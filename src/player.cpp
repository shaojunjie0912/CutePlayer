#include <cuteplayer/logger.hpp>
#include <cuteplayer/player.hpp>
#include <stdexcept>

// NOTE: 一个 AVPacket 可能对应一个或多个 AVFrame (音频)
// 但也可能多个 AVPacket 才可以解码出一个 AVFrame (比如: 视频帧间依赖)

namespace cuteplayer {

// =============================================================================
// Player 实现
// =============================================================================

Player::Player(std::string file_path)
    : file_path_(std::move(file_path)),
      video_packet_queue_(kMaxPacketQueueDataBytes),
      audio_packet_queue_(kMaxPacketQueueDataBytes),
      video_frame_queue_(kMaxFrameQueueSize),  // 默认不保留上一帧
      audio_frame_(av_frame_alloc()) {
    InitSDL();
    OpenInputFile();
    FindStreams();
    if (video_stream_idx_ != -1) {
        OpenStreamComponent(video_stream_idx_);
    }
    if (audio_stream_idx_ != -1) {
        OpenStreamComponent(audio_stream_idx_);
    }
    StartThreads();
}

Player::~Player() {
    if (!stop_.load()) {
        stop_.store(true);  // 停止标志位
    }

    // 关闭队列以唤醒任何等待中的线程
    video_packet_queue_.Close();
    audio_packet_queue_.Close();
    video_frame_queue_.Close();

    // 提前释放与 SDL 相关的资源，再调用 SDL_Quit
    texture_.reset();
    renderer_.reset();
    window_.reset();
    SDL_Quit();
}

void Player::Run() {
    if (!video_stream_ && !audio_stream_) {
        LOG_ERROR("必须同时包含视频流和音频流!");
        return;
    }
    ScheduleNextVideoRefresh(40);  // 40ms 后推一个 VideoRefreshHandler 事件
    SDL_Event event;
    // 主线程阻塞等待事件, 直到收到退出事件
    while (!stop_.load()) {
        SDL_WaitEvent(&event);
        switch (event.type) {
            case SDL_QUIT:
                stop_.store(true);
                return;
            case kFFRefreshEvent:       // 我们自定义的事件类型
                VideoRefreshHandler();  // 视频刷新
                break;
            default:
                break;
        }
    }
}

void Player::InitSDL() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        throw std::runtime_error("SDL 初始化失败: " + std::string(SDL_GetError()));
    }
    // 创建窗口 (unique_ptr 管理)
    window_.reset(SDL_CreateWindow("CutePlayer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   kDefaultWidth, kDefaultHeight,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE));
    if (!window_) {
        throw std::runtime_error("创建窗口失败: " + std::string(SDL_GetError()));
    }
    // 创建渲染器 (unique_ptr 管理)
    renderer_.reset(SDL_CreateRenderer(window_.get(), -1,
                                       SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (!renderer_) {
        throw std::runtime_error("SDL_CreateRenderer Error: " + std::string(SDL_GetError()));
    }
    LOG_INFO("SDL 初始化成功!");
}

void Player::OpenInputFile() {
    LOG_INFO("尝试打开输入文件...");
    AVFormatContext* fmt_ctx{nullptr};
    if (avformat_open_input(&fmt_ctx, file_path_.c_str(), nullptr, nullptr) < 0) {
        throw std::runtime_error("打开输入文件失败: " + file_path_);
    }
    format_ctx_.reset(fmt_ctx);
    if (avformat_find_stream_info(format_ctx_.get(), nullptr) < 0) {
        throw std::runtime_error("获取流信息失败");
    }
    LOG_INFO("成功获取流信息!");
}

void Player::FindStreams() {
    for (unsigned int i = 0; i < format_ctx_->nb_streams; ++i) {
        auto stream = format_ctx_->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_idx_ == -1) {
            video_stream_idx_ = i;
        }
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_idx_ == -1) {
            audio_stream_idx_ = i;
        }
    }
    if (video_stream_idx_ == -1 && audio_stream_idx_ == -1) {
        throw std::runtime_error("未找到音频或视频流");
    }
    LOG_INFO("视频流索引: {}, 音频流索引: {}", video_stream_idx_, audio_stream_idx_);
}

void Player::OpenStreamComponent(int stream_index) {
    std::string stream_type = stream_index == video_stream_idx_ ? "视频" : "音频";
    LOG_INFO("尝试打开<{}>流组件...", stream_type);
    AVStream* stream{format_ctx_->streams[stream_index]};
    AVCodecParameters* codec_params{stream->codecpar};

    // 查找解码器
    const AVCodec* codec{avcodec_find_decoder(codec_params->codec_id)};
    if (!codec) {
        throw std::runtime_error("未找到解码器");
    } else {
        LOG_INFO("找到解码器: {}", avcodec_get_name(codec_params->codec_id));
    }

    // 创建编解码器上下文
    UniqueAVCodecContext codec_context{avcodec_alloc_context3(codec)};
    if (!codec_context) {
        throw std::runtime_error("分配解码器上下文失败");
    }
    // 拷贝参数到编解码器上下文
    if (avcodec_parameters_to_context(codec_context.get(), codec_params) < 0) {
        throw std::runtime_error("拷贝解码器参数至解码器上下文失败");
    }
    // 绑定编解码器和编解码器上下文
    if (avcodec_open2(codec_context.get(), codec, nullptr) < 0) {
        throw std::runtime_error("打开解码器失败");
    }

    if (codec_context->codec_type == AVMEDIA_TYPE_VIDEO) {
        LOG_INFO("视频流组件打开成功!");
        video_stream_ = stream;
        video_codec_ctx_ = std::move(codec_context);
        frame_timer_ = static_cast<double>(av_gettime()) / 1000000.0;
    } else if (codec_context->codec_type == AVMEDIA_TYPE_AUDIO) {
        LOG_INFO("音频流组件打开成功!");
        audio_stream_ = stream;
        audio_codec_ctx_ = std::move(codec_context);

        // 音频重采样初始化
        SDL_AudioSpec wanted_spec, actual_spec;
        SDL_memset(&wanted_spec, 0, sizeof(wanted_spec));

        AVChannelLayout out_ch_layout;
        av_channel_layout_default(&out_ch_layout, 2);  // TODO: 需要拷贝用户的 channel_layout 吗?

        wanted_spec.freq = audio_codec_ctx_->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = out_ch_layout.nb_channels;
        wanted_spec.silence = 0;
        wanted_spec.samples = kSdlAudioBufferSize;
        wanted_spec.callback = AudioCallbackWrapper;
        wanted_spec.userdata = this;

        // 打开音频设备
        if (SDL_OpenAudio(&wanted_spec, &actual_spec) < 0) {
            throw std::runtime_error("SDL_OpenAudio 失败: " + std::string(SDL_GetError()));
        }
        LOG_INFO("SDL 音频设备启动成功!");
        // 如果音频格式不是 S16, 则需要重采样
        if (audio_codec_ctx_->sample_fmt != AV_SAMPLE_FMT_S16) {
            LOG_INFO("音频格式不是 S16, 需要重采样...");
            // C++ 的 RAII 智能指针与 C 风格的“出参”函数正确地协同工作: 临时裸指针作为「中间人」
            SwrContext* tmp_swr_ctx{nullptr};
            // Setup resampler
            swr_alloc_set_opts2(&tmp_swr_ctx, &out_ch_layout, AV_SAMPLE_FMT_S16, actual_spec.freq,
                                &audio_codec_ctx_->ch_layout, audio_codec_ctx_->sample_fmt,
                                audio_codec_ctx_->sample_rate, 0, nullptr);
            audio_swr_ctx_.reset(tmp_swr_ctx);  // 立即转移所有权
            swr_init(audio_swr_ctx_.get());
            LOG_INFO("音频重采样上下文创建成功!");
        }
    }
}

// 往 VideoPacketQueue 和 AudioPacketQueue 中添加数据包
void Player::ReadLoop() {
    LOG_INFO("读取线程开始");
    // NOTE: 只分配一次 AVPacket 内存, 后面复用, 因此需要 unref
    // AVPacket 结构体中有一个 AVBufferRef* 指针, 指向数据缓冲区
    UniqueAVPacket packet_template{av_packet_alloc()};  // 用于循环读取的“模板”
    while (!stop_.load()) {
        // av_read_frame: 分配新的一个数据包的内存, 并使得 packet 中的数据指针指向它
        // NOTE: 大小可变!!!
        int ret = av_read_frame(format_ctx_.get(), packet_template.get());
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                LOG_INFO("文件读取完毕!");
            } else {
                LOG_ERROR("读取数据包失败: {}", av_err2str(ret));
            }
            // NOTE: 不需要unref, 因为ret<0时 av_read_frame内部会做清理工作
            break;
        }
        if (packet_template->stream_index == video_stream_idx_ ||
            packet_template->stream_index == audio_stream_idx_) {
            // 创建一个新的 AVPacket 用于放入队列
            UniqueAVPacket packet_to_queue{av_packet_alloc()};
            av_packet_move_ref(packet_to_queue.get(), packet_template.get());  // 移动
            if (packet_to_queue->stream_index == video_stream_idx_) {
                video_packet_queue_.Push(std::move(packet_to_queue));
            } else {
                audio_packet_queue_.Push(std::move(packet_to_queue));
            }
        } else {
            // 无论是不是需要的流, 都要 unref
            av_packet_unref(packet_template.get());  // packet 上次的内存块引用计数为0就自动释放
        }
    }
    video_packet_queue_.Close();
    audio_packet_queue_.Close();
    LOG_INFO("读取线程结束");
}

// 返回的是解码音频数据字节数
int Player::DecodeAudioFrame() {
    while (!stop_.load()) {
        // NOTE: 非阻塞, 不能阻塞 SDL 音频回调, 否则用户会听到清晰可闻的音频爆音/卡顿/断续
        // 任何情况下, 音频回调函数都必须严格避免任何可能导致阻塞或长时间运行的操作
        auto packet{audio_packet_queue_.TryPop()};
        if (!packet) {
            return 0;  // 静音
        }

        // avcodec_send_packet: 异步发送一个 AVPacket 到解码器(解码器内部维护一个 AVPacket 队列)
        int ret = avcodec_send_packet(audio_codec_ctx_.get(), packet->get());
        // TODO: 此处 ret==0 表示成功, 是否需要 unref? 因为 packet 是一个 RAII 类
        // if (ret == 0) {
        //     av_packet_unref(packet->get());
        // }
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {  // 解码器内部缓冲区已满, 先取走一些 AVFrame
                LOG_DEBUG("音频 avcodec_send_packet 需要 receive, ret: EAGAIN");
            } else {  // EOF 或致命错误
                LOG_ERROR("音频 avcodec_send_packet EOF/发生错误: {}", av_err2str(ret));
                av_packet_unref(packet->get());
                return ret;
            }
        }

        // 循环调用 avcodec_receive_frame 以获取所有可能产生的帧 (0,1,...)
        while (!stop_.load()) {
            ret = avcodec_receive_frame(audio_codec_ctx_.get(), audio_frame_.get());
            if (ret < 0) {
                if (ret == AVERROR(EAGAIN)) {  // 需要更多 packet
                    break;
                } else {  // 解码结束或致命错误
                    LOG_ERROR("音频 avcodec_receive_frame EOF/发生错误: {}", av_err2str(ret));
                    av_frame_unref(audio_frame_.get());  // 清空 frame 的引用计数
                    return -1;
                }
            }
            // 正常情况
            int data_bytes{0};
            auto in = static_cast<uint8_t* const*>(audio_frame_.get()->extended_data);
            int in_count = audio_frame_.get()->nb_samples;
            // 256 是一个安全余量, 因为重采样过程中可能会有轻微的延迟和缓存,
            // 导致输出样本数略多于输入
            int out_count = audio_frame_.get()->nb_samples + 256;

            // 重采样后输出缓冲区大小 = 2 * 2 * audio_frame_.nb_samples
            int out_size =
                av_samples_get_buffer_size(nullptr, audio_frame_.get()->ch_layout.nb_channels,
                                           out_count, AV_SAMPLE_FMT_S16, 0);
            // 重新分配 audio_buffer_ 内存
            audio_buffer_.resize(out_size);
            auto out = audio_buffer_.data();

            // 重采样 -> 返回每个通道的样本数
            int nb_ch_samples = swr_convert(audio_swr_ctx_.get(), &out, out_count, in, in_count);

            // NOTE: 计算解码后的音频数据字节数
            // 每个通道的样本数 * 通道数 * 每个样本的字节数(S16=2字节)
            data_bytes = nb_ch_samples * audio_frame_.get()->ch_layout.nb_channels *
                         av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

            // NOTE: 更新音频时钟!!!  = pts + 持续时长
            if (audio_frame_.get()->pts != AV_NOPTS_VALUE) {
                // 获取音频流的时间基
                AVRational time_base = audio_stream_->time_base;

                // 计算当前帧的持续时长 (秒) = 样本数 / 采样率
                auto duration = static_cast<double>(audio_frame_.get()->nb_samples) /
                                audio_frame_.get()->sample_rate;

                // 将 pts 转换为秒，然后加上持续时长
                audio_clock_ = audio_frame_.get()->pts * av_q2d(time_base) + duration;
            } else {
                audio_clock_ = NAN;
            }
            av_frame_unref(audio_frame_.get());  // 清空 frame 的引用计数
            return data_bytes;
        }
    }
    return 0;
}

// 音频回调函数(由 SDL 创建线程)
// userdata: 用户数据
// stream: 音频数据流(注意: 音频设备从该流中获取数据)
// len: 需要填充的数据长度
void Player::AudioCallbackWrapper(void* userdata, uint8_t* stream, int len) {
    static_cast<Player*>(userdata)->AudioCallback(stream, len);
}

// TODO:
// stream: 音频数据流(注意: 音频设备从该流中获取数据)
// len: 需要填充的数据长度
void Player::AudioCallback(uint8_t* stream, int len) {
    std::memset(stream, 0, len);  // 安全措施: 静音填充

    // 还需要 len 字节的数据
    while (len > 0) {
        // 已经发送我们所有的数据，需要获取更多数据
        if (audio_buf_index_ >= audio_buf_size_) {
            int decoded_size = DecodeAudioFrame();
            if (decoded_size <= 0) {
                // Error/EOF
                return;
            }
            audio_buf_size_ = decoded_size;
            audio_buf_index_ = 0;
        }

        int len_to_copy = std::min(len, static_cast<int>(audio_buf_size_ - audio_buf_index_));
        std::memcpy(stream, audio_buffer_.data() + audio_buf_index_, len_to_copy);

        len -= len_to_copy;
        stream += len_to_copy;
        audio_buf_index_ += len_to_copy;
    }
}

void Player::StartThreads() {
    read_thread_ = std::jthread{[this] { ReadLoop(); }};                 // 启动读取线程
    video_decode_thread_ = std::jthread{[this] { VideoDecodeLoop(); }};  // 启动视频解码线程
    SDL_PauseAudio(0);                                                   // 启动音频回调
}

int Player::DecodeVideoFrame() {
    UniqueAVFrame frame{av_frame_alloc()};
    auto frame_rate = video_stream_->avg_frame_rate;  // 帧率
    while (!stop_.load()) {
        auto packet = video_packet_queue_.Pop();  // 阻塞式
        if (packet) {                             // 成功获取到包
            int ret = avcodec_send_packet(video_codec_ctx_.get(), packet->get());
            if (ret < 0) {
                LOG_ERROR("视频 avcodec_send_packet 发生错误: {}", av_err2str(ret));
                // 即使发送失败，也尝试继续解码，可能只是需要先 receive
            }
        } else {  // 如果返回空指针, 说明队列已关闭, 这是来自 ReadLoop 的 EOF 信号
            LOG_INFO("视频包队列已关闭, 发送 null packet 以冲刷解码器。");
            avcodec_send_packet(video_codec_ctx_.get(), nullptr);
        }

        while (!stop_.load()) {
            int ret = avcodec_receive_frame(video_codec_ctx_.get(), frame.get());
            if (ret < 0) {
                if (ret == AVERROR(EAGAIN)) {  // 需要更多 packet
                    break;
                } else if (ret == AVERROR_EOF) {  // 解码器已完全冲刷, 所有帧已取出
                    LOG_INFO("视频解码器冲刷完毕, 关闭视频帧队列!");
                    video_frame_queue_.Close();  // 关闭帧队列, NOTE: 通知渲染逻辑
                    return 0;                    // 成功退出解码线程
                } else {
                    LOG_ERROR("视频 avcodec_receive_frame 发生致命错误: {}", av_err2str(ret));
                    video_frame_queue_.Close();  // 出错也要关闭, 防止渲染线程死锁
                    // av_frame_unref(frame.get());
                    return -1;
                }
            }

            // ================== 音视频同步 ==================
            // (尝试)获取解码后的帧的 pts
            double pts =
                (frame->pts == AV_NOPTS_VALUE) ? 0 : frame->pts * av_q2d(video_stream_->time_base);
            pts = SynchronizeVideo(frame.get(), pts);
            // 计算当前帧的时长
            auto duration = (frame_rate.num && frame_rate.den
                                 ? av_q2d(AVRational{frame_rate.den, frame_rate.num})
                                 : 0);
            // 写入视频帧环形队列 (阻塞)
            auto decoded_frame = video_frame_queue_.PeekWritable();
            if (!decoded_frame) {
                // 如果返回 nullptr，说明队列已关闭，线程应立即退出
                LOG_INFO("视频帧环形队列已关闭, 解码线程退出!");
                return 0;
            }
            decoded_frame->pts_ = pts;
            decoded_frame->duration_ = duration;
            decoded_frame->sar_ = frame->sample_aspect_ratio;
            decoded_frame->width_ = frame->width;
            decoded_frame->height_ = frame->height;
            decoded_frame->format_ = frame->format;
            decoded_frame->pos_ = AV_NOPTS_VALUE;  // TODO: 这里为什么设置为 AV_NOPTS_VALUE?
            av_frame_move_ref(decoded_frame->frame_.get(), frame.get());  // 移动
            video_frame_queue_.MoveWriteIndex();
        }
        if (!packet) {
            // 如果已经发送了 null packet 并且内部循环因 EAGAIN 退出，
            // 说明解码器已经没有更多帧可以输出了。
            // 虽然没有收到 AVERROR_EOF，但也可以安全地认为解码过程已结束。
            LOG_INFO("视频解码器已无更多帧输出，关闭视频帧队列。");
            video_frame_queue_.Close();
            return 0;  // 成功退出解码线程
        }
    }
    video_frame_queue_.Close();  // 确保任何退出路径都会关闭队列
    return 0;
}

void Player::VideoDecodeLoop() {
    LOG_INFO("视频解码线程开始!");
    if (DecodeVideoFrame() < 0) {
        throw std::runtime_error("视频帧解码失败!");
    }
    LOG_INFO("视频解码线程结束!");
}

// 视频时钟计算!
double Player::SynchronizeVideo(const AVFrame* frame, double pts) {
    if (pts != 0) {
        // 如果解码出的帧带有有效的 pts, 则更新视频时钟
        video_clock_ = pts;
    } else {
        // 如果解码出的帧没有 pts, 就沿用上一帧的 video_clock_
        pts = video_clock_;
    }
    double duration{.0};  // 帧持续时间 = 1/帧率
    auto frame_rate = video_stream_->avg_frame_rate;
    if (frame_rate.num != 0 && frame_rate.den != 0) {
        duration = 1.0 / av_q2d(frame_rate);
    } else {
        // 如果无法从流中获取有效的帧率，则使用一个默认的、常见的帧延迟。
        // 例如 0.04 秒对应 25fps。这是一种降级策略。
        duration = 0.04;
    }
    // 某些编码格式或视频流会通过 repeat_pict 字段提示一帧需要被额外显示。
    // 这里根据这个字段来微调帧的持续时间，以更好地匹配视频的原始意图。
    // FFmpeg官方播放器ffplay中也使用了类似的逻辑。
    double frame_delay = duration + frame->repeat_pict * (duration * 0.5);

    // 在当前视频时钟的基础上，加上一帧的持续时间，
    // 得到下一帧的理论显示时间戳，并更新视频时钟。
    video_clock_ += frame_delay;
    return pts;
}

void Player::ScheduleNextVideoRefresh(int delay_ms) {
    SDL_AddTimer(delay_ms, VideoRefreshTimerWrapper, this);
}

uint32_t Player::VideoRefreshTimerWrapper(uint32_t /*interval*/, void* opaque) {
    SDL_Event event;
    event.type = kFFRefreshEvent;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0;
}

// 核心视频时钟->音频时钟同步逻辑
void Player::VideoRefreshHandler() {
    if (stop_.load()) {
        return;
    }
    if (!video_stream_) {               // 如果还没有视频流, 考虑等一会
        ScheduleNextVideoRefresh(100);  // 重新推入事件, 等待视频流
        return;
    }

    // 阻塞获取当前可读 DecodedFrame 指针
    auto decoded_frame = video_frame_queue_.PeekReadable();
    if (!decoded_frame) {
        // 当帧队列关闭且为空时 PeekReadable 会返回 nullptr,
        // 这意味着所有帧都已渲染完毕，播放正式结束。
        LOG_INFO("视频播放完毕, 发送退出事件");
        stop_.store(true);  // 设置停止标志
        // 主动推送一个退出事件来优雅地终止主事件循环
        SDL_Event event;
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
        return;
    }
    double pts = decoded_frame->pts_;
    double delay = frame_last_pts_ == 0 ? 0 : pts - frame_last_pts_;  // 计算两帧之间的理论间隔
    if (delay <= 0 || delay >= 1.0) {  // 如果间隔小于0或大于1秒, 则使用上一帧的间隔
        delay = frame_last_delay_;
    }
    frame_last_delay_ = delay;
    frame_last_pts_ = pts;

    // ======================== 核心同步逻辑 =======================
    double ref_clock = GetMasterClock();  // 获取参考时钟
    double diff = pts - ref_clock;        // 计算当前视频帧的 pts 与参考时钟的差值
    // 动态调整同步阈值 (阈值至少是MIN，但不超过MAX，并与帧延迟相关联，是ffplay的经典做法)
    double sync_threshold = std::max(kMinAvSyncThreshold, std::min(kMaxAvSyncThreshold, delay));
    if (!isnan(diff) && std::abs(diff) < kAvNoSyncThreshold) {
        if (diff <= -sync_threshold) {
            // NOTE: 丢帧逻辑
            // 视频严重落后(diff为一个较大的负数)，需要丢帧来追赶。
            // 我们简单地移动读指针，相当于丢弃当前帧，然后重新调度以处理下一帧。
            video_frame_queue_.MoveReadIndex();  // 里面有 frame unref
            // 立即重新调度，尽快处理下一帧
            ScheduleNextVideoRefresh(0);
            return;  // 注意：丢帧后直接返回，不进行本轮的渲染
        }
        if (diff >= sync_threshold) {
            // 视频超前，需要增加延迟等待音频。
            // 将理论延迟加倍是一种简单有效的策略。
            delay = delay * 2;
        }
    }

    // 计算并安排下一次刷新
    frame_timer_ += delay;  // TODO: 他的初始化在哪? 这个定时器有啥作用?
    double actual_delay = frame_timer_ - (static_cast<double>(av_gettime()) / 1000000.0);
    // 确保延迟有一个最小值，防止CPU空转
    if (actual_delay < 0.010) {
        actual_delay = 0.010;
    }
    // 安排下一次定时器回调
    ScheduleNextVideoRefresh(static_cast<int>(actual_delay * 1000 + 0.5));
    RenderVideoFrame();
}

void Player::RenderVideoFrame() {
    auto decoded_frame = video_frame_queue_.PeekReadable();
    if (!decoded_frame) {  // 一般不会没有吧
        LOG_ERROR("RenderVideoFrame: 没有可读的视频解码帧!");
        return;
    }

    const AVFrame* frame = decoded_frame->frame_.get();

    if (!texture_) {
        texture_.reset(SDL_CreateTexture(renderer_.get(), SDL_PIXELFORMAT_IYUV,
                                         SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height));
        if (!texture_) {
            LOG_ERROR("RenderVideoFrame: 创建 SDL 纹理失败: {}", SDL_GetError());
            return;
        }
    }

    SDL_UpdateYUVTexture(texture_.get(), nullptr, frame->data[0], frame->linesize[0],
                         frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]);

    SDL_Rect rect;
    // 计算显示区域
    CalculateDisplayRect(&rect, window_x_, window_y_, window_width_, window_height_, frame->width,
                         frame->height, frame->sample_aspect_ratio);

    // 渲染视频帧
    SDL_RenderClear(renderer_.get());
    SDL_RenderCopy(renderer_.get(), texture_.get(), nullptr, &rect);
    SDL_RenderPresent(renderer_.get());
    video_frame_queue_.MoveReadIndex();  // 释放视频帧
}

double Player::GetMasterClock() const {
    if (audio_stream_) {
        return audio_clock_;
    }
    return GetVideoClock();
}

double Player::GetVideoClock() const { return video_clock_; }

void Player::CalculateDisplayRect(SDL_Rect* rect, int window_x, int window_y, int window_width,
                                  int window_height, int picture_width, int picture_height,
                                  AVRational picture_sar) {
    // NOTE: picture_sar: sample aspect ratio 像素宽高比 SAR
    // 不同于 DAR(display aspect ratio) 显示宽高比
    AVRational aspect_ratio = picture_sar;

    // 如果 pic_sar 为零或负值（即无效值），则将 aspect_ratio 设置为 1:1，表示无畸变的方形像素。
    if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0) {
        aspect_ratio = av_make_q(1, 1);
    }

    // 计算显示的宽高比, 根据图像原始尺寸和像素宽高比计算
    // NOTE: 显示宽高比(DAR) = 像素宽高比(SAR) * 图像宽高比
    aspect_ratio = av_mul_q(aspect_ratio, av_make_q(picture_width, picture_height));

    // 计算显示的宽高
    // 首先尝试让视频的高度填满窗口，并计算出此时应有的宽度
    int64_t height = window_height;
    int64_t width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1;
    // 如果越界了，就说明视频的宽度才是限制因素。
    // 于是反过来让视频的宽度填满窗口，并重新计算出此时应有的高度。
    if (width > window_width) {
        width = window_width;
        height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1;
    }

    // 计算显示的位置
    // 窗口尺寸减去视频尺寸，得到总的黑边大小，再除以2，
    // 就得到了让其居中的左上角 (x, y) 坐标。
    int64_t x = (window_width - width) / 2;
    int64_t y = (window_height - height) / 2;

    rect->x = static_cast<int>(window_x + x);
    rect->y = static_cast<int>(window_y + y);

    rect->w = std::max(static_cast<int>(width), 1);
    rect->h = std::max(static_cast<int>(height), 1);
}

}  // namespace cuteplayer