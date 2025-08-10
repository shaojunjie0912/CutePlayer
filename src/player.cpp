#include <condition_variable>
#include <cuteplayer/player.hpp>
#include <stdexcept>

// TODO: 没有 std::stop_token 就用 stop 标志位
// NOTE: 一个 AVPacket 可能对应一个或多个 AVFrame
// 但也可能多个 AVPacket 才可以解码出一个 AVFrame (比如: 帧间依赖)
// TODO: 仔细思考每个分支的返回值

namespace cuteplayer {

// =============================================================================
// PacketQueue 实现
// =============================================================================

PacketQueue::PacketQueue(std::size_t data_size_limit) : max_data_bytes_(data_size_limit) {}

bool PacketQueue::Push(UniqueAVPacket packet) {
    std::unique_lock lk{mtx_};
    // TODO: 不提前加 packet->size 为了避免潜在的饥饿/死锁
    cv_can_push_.wait(lk, [this] { return closed_ || curr_data_bytes_ < max_data_bytes_; });
    if (closed_) {
        return false;
    }
    curr_data_bytes_ += packet->size;
    queue_.push(std::move(packet));
    cv_can_pop_.notify_one();
    return true;
}

std::optional<UniqueAVPacket> PacketQueue::Pop() {
    std::unique_lock lk{mtx_};
    cv_can_pop_.wait(lk, [this] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {  // TODO: 如果队列为空, 那一定是因为被关闭了
        return std::nullopt;
    }
    auto packet{std::move(queue_.front())};
    queue_.pop();
    curr_data_bytes_ -= packet->size;
    cv_can_push_.notify_one();
    return packet;
}

std::optional<UniqueAVPacket> PacketQueue::TryPop() {
    std::unique_lock lk{mtx_};
    if (queue_.empty()) {
        return std::nullopt;
    }
    auto packet{std::move(queue_.front())};
    queue_.pop();
    curr_data_bytes_ -= packet->size;
    cv_can_push_.notify_one();
    return packet;
}

void PacketQueue::Clear() {
    std::unique_lock lk{mtx_};
    std::queue<UniqueAVPacket>{}.swap(queue_);
    curr_data_bytes_ = 0;
    cv_can_push_.notify_all();  // 因为清空了队列, 需要通知所有可能在等待的生产者
}

void PacketQueue::Close() {
    std::unique_lock lk{mtx_};
    if (closed_) {
        return;
    }
    closed_ = true;
    // 唤醒所有可能在等待的生产者和消费者线程
    cv_can_push_.notify_all();
    cv_can_pop_.notify_all();
}

std::size_t PacketQueue::GetTotalDataSize() const {
    std::lock_guard lk{mtx_};
    return curr_data_bytes_;
}

// =============================================================================
// FrameQueue 实现
// =============================================================================

FrameQueue::FrameQueue(std::size_t max_size, bool keep_last_frame)
    : max_size_(std::min<size_t>(16, max_size)),  // TODO: 固定 16 取小
      decoded_frames_(max_size_),
      keep_last_frame_(keep_last_frame) {
    for (auto& decoded_frame : decoded_frames_) {
        decoded_frame.frame.reset(av_frame_alloc());
        if (!decoded_frame.frame) {
            throw std::runtime_error("分配 AVFrame 失败");
        }
    }
}

DecodedFrame* FrameQueue::PeekWritable() {
    std::unique_lock lk{mtx_};
    cv_can_push_.wait(lk, [this] { return size_ < max_size_; });
    return &decoded_frames_[windex_];
}

DecodedFrame* FrameQueue::PeekReadable() {
    std::unique_lock lk{mtx_};
    cv_can_pop_.wait(lk, [this] { return size_ > rindex_shown_; });
    return &decoded_frames_[(rindex_ + rindex_shown_) % max_size_];
}

// 获取当前可读取的帧，而不改变队列状态。
// 渲染线程在渲染当前帧时使用，不会修改队列状态。
// HACK: 读取索引 + 读取索引偏移
DecodedFrame* FrameQueue::Peek() {
    std::lock_guard lk{mtx_};  // TODO: 锁是否必要?
    return &decoded_frames_[(rindex_ + rindex_shown_) % max_size_];
}

// TODO: PeekReadable 后调用, 是否可以合并?
void FrameQueue::Pop() {
    std::unique_lock lk{mtx_};
    if (keep_last_frame_ && rindex_shown_ == 0) {
        rindex_shown_ = 1;
        return;
    }
    av_frame_unref(decoded_frames_[rindex_].frame.get());
    if (++rindex_ >= max_size_) {
        rindex_ = 0;
    }
    --size_;
    cv_can_push_.notify_one();
}

// TODO: PeekWritable 后调用, 是否可以合并?
void FrameQueue::Push() {
    std::unique_lock lk{mtx_};
    if (++windex_ == max_size_) {
        windex_ = 0;
    }
    ++size_;
    cv_can_pop_.notify_one();
}

std::size_t FrameQueue::GetSize() const {
    std::lock_guard lk{mtx_};
    return size_;
}

// =============================================================================
// Player 实现
// =============================================================================

Player::Player(std::string file_path)
    : file_path_(std::move(file_path)),
      video_packet_queue_(kMaxPacketQueueDataBytes),
      audio_packet_queue_(kMaxPacketQueueDataBytes),
      video_frame_queue_(kMaxFrameQueueSize, true),
      audio_frame_(av_frame_alloc()),
      running_(true) {
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
    // 通知线程退出
    running_.store(false);

    // 关闭队列以唤醒任何等待中的线程
    video_packet_queue_.Close();
    audio_packet_queue_.Close();

    // 停止音频回调
    SDL_PauseAudio(1);

    // 推送一个退出事件以唤醒可能阻塞在 SDL_WaitEvent 的线程
    SDL_Event quit_event;
    quit_event.type = SDL_QUIT;
    SDL_PushEvent(&quit_event);

    // 提前释放与 SDL 相关的资源，再调用 SDL_Quit
    texture_.reset();
    renderer_.reset();
    window_.reset();

    // 关闭 SDL 子系统
    SDL_Quit();
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
    LOG_INFO("SDL 初始化成功");
}

void Player::OpenInputFile() {
    LOG_INFO("打开输入文件");
    AVFormatContext* fmt_ctx{nullptr};
    if (avformat_open_input(&fmt_ctx, file_path_.c_str(), nullptr, nullptr) < 0) {
        throw std::runtime_error("打开输入文件失败: " + file_path_);
    }
    format_ctx_.reset(fmt_ctx);
    if (avformat_find_stream_info(format_ctx_.get(), nullptr) < 0) {
        throw std::runtime_error("获取流信息失败");
    }
    LOG_INFO("成功打开输入文件, 成功获取流信息!");
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
    LOG_INFO("音频流索引: {}, 视频流索引: {}", audio_stream_idx_, video_stream_idx_);
}

void Player::OpenStreamComponent(int stream_index) {
    LOG_INFO("打开{}流组件", stream_index == video_stream_idx_ ? "视频" : "音频");
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
        video_stream_ = stream;
        video_codec_ctx_ = std::move(codec_context);
        frame_timer_ = static_cast<double>(av_gettime()) / 1000000.0;
        // TODO: 少一个视频线程吗?
        LOG_INFO("视频流组件打开成功");
    } else if (codec_context->codec_type == AVMEDIA_TYPE_AUDIO) {
        audio_stream_ = stream;
        audio_codec_ctx_ = std::move(codec_context);

        // 音频重采样初始化
        SDL_AudioSpec wanted_spec, actual_spec;
        SDL_memset(&wanted_spec, 0, sizeof(wanted_spec));

        AVChannelLayout out_ch_layout;
        av_channel_layout_default(&out_ch_layout, 2);  // NOTE: 强制立体声, 没有拷贝用户的

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
        // 如果音频格式不是 S16, 则需要重采样
        if (audio_codec_ctx_->sample_fmt != AV_SAMPLE_FMT_S16) {
            // C++ 的 RAII 智能指针与 C 风格的“出参”函数正确地协同工作: 临时裸指针作为「中间人」
            SwrContext* tmp_swr_ctx{nullptr};
            // Setup resampler
            swr_alloc_set_opts2(&tmp_swr_ctx, &out_ch_layout, AV_SAMPLE_FMT_S16, actual_spec.freq,
                                &audio_codec_ctx_->ch_layout, audio_codec_ctx_->sample_fmt,
                                audio_codec_ctx_->sample_rate, 0, nullptr);
            audio_swr_ctx_.reset(tmp_swr_ctx);  // 立即转移所有权
            swr_init(audio_swr_ctx_.get());
        }
        LOG_INFO("音频流组件打开成功, SDL 音频设备启动");
    }
}

// 往 VideoPacketQueue 和 AudioPacketQueue 中添加数据包
void Player::ReadLoop() {
    LOG_INFO("读取线程开始");
    // NOTE: 只分配一次 AVPacket 内存, 后面复用, 因此需要 unref
    // AVPacket 结构体中有一个 AVBufferRef* 指针, 指向数据缓冲区
    UniqueAVPacket packet_template{av_packet_alloc()};  // 用于循环读取的“模板”
    while (running_.load()) {
        // NOTE: 背压机制, 限制队列大小(字节数)
        if (video_packet_queue_.GetTotalDataSize() > kMaxPacketQueueDataBytes ||
            audio_packet_queue_.GetTotalDataSize() > kMaxPacketQueueDataBytes) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        // av_read_frame: 分配新的一个数据包的内存, 并使得 packet 中的数据指针指向它
        // NOTE: 大小可变!!!
        int ret = av_read_frame(format_ctx_.get(), packet_template.get());
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                LOG_INFO("文件读取完毕!");
            } else {
                LOG_ERROR("读取数据包失败: {}", av_err2str(ret));
            }
            break;
        }
        if (packet_template->stream_index == video_stream_idx_ ||
            packet_template->stream_index == audio_stream_idx_) {
            // 创建一个新的 AVPacket 用于放入队列
            // 这是最高效的方式：创建一个对 packet_template 的引用，而不是克隆
            // TODO: 那 packet_to_queue 里面的内存就由队列消费者释放?
            UniqueAVPacket packet_to_queue{av_packet_alloc()};
            if (av_packet_ref(packet_to_queue.get(), packet_template.get()) < 0) {
                // 引用失败，通常是内存不足
                LOG_ERROR("av_packet_ref 失败, 内存不足");
                break;
            }
            if (packet_template->stream_index == video_stream_idx_) {
                video_packet_queue_.Push(std::move(packet_to_queue));
            } else {
                audio_packet_queue_.Push(std::move(packet_to_queue));
            }
        }
        // 无论是不是需要的流, 都要 unref
        av_packet_unref(packet_template.get());  // packet 上次的内存块引用计数为0就自动释放
    }

    // NOTE: 读取线程结束,
    SDL_Event quit_event;
    quit_event.type = SDL_QUIT;
    SDL_PushEvent(&quit_event);

    LOG_INFO("读取线程结束");
}

// 返回的是音频帧的数量
int Player::DecodeAudioFrame() {
    while (running_.load()) {
        // TODO: TryPop?
        auto packet{audio_packet_queue_.TryPop()};
        if (!packet) {
            // TODO: 音频包队列为空 应该返回 0 静音 还是 等待后再重新取
            return 0;
        }

        // avcodec_send_packet: 异步发送一个 AVPacket 到解码器(解码器内部维护一个 AVPacket 队列)
        int ret = avcodec_send_packet(audio_codec_ctx_.get(), packet->get());

        if (ret == 0) {  // 发送成功
            av_packet_unref(packet->get());
        } else if (ret == AVERROR(EAGAIN)) {  // 解码器内部缓冲区已满, 先取走一些 AVFrame
            LOG_DEBUG("音频 avcodec_send_packet 需要 receive, ret: EAGAIN");
        } else {  // EOF 或致命错误
            LOG_ERROR("音频 avcodec_send_packet EOF/发生错误: {}", av_err2str(ret));
            av_packet_unref(packet->get());
            return ret;
        }

        // 循环调用 avcodec_receive_frame 以获取所有可能产生的帧 (0,1,...)
        while (running_.load()) {
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
            int frame_cnt{0};
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
            frame_cnt = nb_ch_samples * audio_frame_.get()->ch_layout.nb_channels *
                        av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

            // NOTE: 更新音频时钟!!!  = 起始时间 + 持续时长
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
            return frame_cnt;
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

// TODO: 存
// stream: 音频数据流(注意: 音频设备从该流中获取数据)
// len: 需要填充的数据长度
void Player::AudioCallback(uint8_t* stream, int len) {
    SDL_memset(stream, 0, len);

    // 缓冲区没有数据了
    while (len > 0) {
        // 已经发送我们所有的数据，需要获取更多数据
        // TODO: 这里的 audio_buf_size_ 跟 audio_buffer_.size() 是否不对应?
        if (audio_buf_index_ >= audio_buf_size_) {
            int decoded_size = DecodeAudioFrame();
            if (decoded_size <= 0) {
                // Error or EOF, 填充静音
                return;
            }
            audio_buf_size_ = decoded_size;
            audio_buf_index_ = 0;
        }

        int len_to_copy = std::min(len, static_cast<int>(audio_buf_size_ - audio_buf_index_));
        SDL_memcpy(stream, audio_buffer_.data() + audio_buf_index_, len_to_copy);

        len -= len_to_copy;
        stream += len_to_copy;
        audio_buf_index_ += len_to_copy;
    }
}

void Player::StartThreads() {
    // 启动读取线程
    read_thread_ = std::jthread{[this] { ReadLoop(); }};
    // 如果存在视频流, 则启动视频解码线程
    if (video_stream_) {
        video_decode_thread_ = std::jthread{[this] { VideoDecodeLoop(); }};
    }
    // 启动音频回调
    if (audio_stream_) {
        SDL_PauseAudio(0);
    }
}

void Player::VideoDecodeLoop() {
    LOG_INFO("视频解码线程开始");
    UniqueAVFrame frame{av_frame_alloc()};
    while (running_.load()) {
        auto packet = video_packet_queue_.TryPop();
        // TODO: 为什么视频就等待而音频直接return了
        if (!packet) {
            LOG_DEBUG("没有获取到视频包, 等待 10 ms...");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int ret = avcodec_send_packet(video_codec_ctx_.get(), packet->get());
        if (ret == 0) {  // 发送成功
            av_packet_unref(packet->get());
        } else if (ret == AVERROR(EAGAIN)) {  // 解码器内部缓冲区已满, 先取走一些 AVFrame
            LOG_DEBUG("视频 avcodec_send_packet 需要 receive, ret: EAGAIN");
        } else {  // EOF 或致命错误
            LOG_ERROR("视频 avcodec_send_packet EOF/发生错误: {}", av_err2str(ret));
            av_packet_unref(packet->get());
            return;
        }

        while (running_.load()) {
            ret = avcodec_receive_frame(video_codec_ctx_.get(), frame.get());
            if (ret < 0) {
                if (ret == AVERROR(EAGAIN)) {  // 需要更多 packet
                    break;
                } else {
                    LOG_ERROR("视频 avcodec_receive_frame EOF/发生错误: {}", av_err2str(ret));
                    av_frame_unref(frame.get());
                    return;
                }
            }
            double pts =
                (frame->pts == AV_NOPTS_VALUE) ? 0 : frame->pts * av_q2d(video_stream_->time_base);
            pts = SynchronizeVideo(frame.get(), pts);

            // TODO: 以下代码可能不正确, 内存泄露/不必要的拷贝
            auto writable_frame_opt = video_frame_queue_.PeekWritable();
            if (writable_frame_opt) {
                DecodedFrame* df = writable_frame_opt;
                av_frame_move_ref(df->frame.get(), frame.get());
                df->pts = pts;
                video_frame_queue_.Push();
            }
            av_frame_unref(frame.get());
        }
    }
    LOG_INFO("视频解码线程结束");
}

uint32_t Player::VideoRefreshTimerWrapper(uint32_t /*interval*/, void* opaque) {
    SDL_Event event;
    event.type = kFFRefreshEvent;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0;  // The timer will not repeat
}

void Player::ScheduleNextVideoRefresh(int delay_ms) {
    SDL_AddTimer(delay_ms, VideoRefreshTimerWrapper, this);
}

void Player::VideoRefreshHandler() {
    if (!video_stream_) {  // TODO: 是否要考虑停止标志位
        return;
    }

    auto frame_opt = video_frame_queue_.PeekReadable();
    if (!frame_opt) {
        ScheduleNextVideoRefresh(1);  // No frame, try again quickly
        return;
    }

    const DecodedFrame* current_frame = frame_opt;
    double pts = current_frame->pts;
    double delay = pts - frame_last_pts_;
    if (delay <= 0 || delay >= 1.0) {
        delay = frame_last_delay_;
    }
    frame_last_delay_ = delay;
    frame_last_pts_ = pts;

    double ref_clock = GetMasterClock();
    double diff = pts - ref_clock;

    double sync_threshold = std::max(delay, kMinAvSyncThreshold);
    if (std::abs(diff) < kAvNoSyncThreshold) {
        if (diff <= -sync_threshold) {  // video is behind
            delay = std::max(0.0, delay + diff);
        } else if (diff >= sync_threshold) {  // video is ahead
            delay = delay + diff;
        }
    }

    frame_timer_ += delay;
    double actual_delay = frame_timer_ - (static_cast<double>(av_gettime()) / 1000000.0);
    if (actual_delay < 0.010) {
        actual_delay = 0.010;
    }

    ScheduleNextVideoRefresh(static_cast<int>(actual_delay * 1000 + 0.5));

    RenderVideoFrame();
    video_frame_queue_.Pop();
}

void Player::RenderVideoFrame() {
    auto frame_opt = video_frame_queue_.PeekReadable();
    if (!frame_opt) return;

    const AVFrame* frame = frame_opt->frame.get();

    if (!texture_) {
        texture_.reset(SDL_CreateTexture(renderer_.get(), SDL_PIXELFORMAT_IYUV,
                                         SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height));
        if (!texture_) {
            LOG_ERROR("Cannot create SDL texture: {}", SDL_GetError());
            return;
        }
    }

    SDL_UpdateYUVTexture(texture_.get(), nullptr, frame->data[0], frame->linesize[0],
                         frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]);

    SDL_Rect rect;
    CalculateDisplayRect(&rect, frame->width, frame->height, frame->sample_aspect_ratio);

    SDL_RenderClear(renderer_.get());
    SDL_RenderCopy(renderer_.get(), texture_.get(), nullptr, &rect);
    SDL_RenderPresent(renderer_.get());
}

double Player::SynchronizeVideo(const AVFrame* frame, double pts) {
    if (pts != 0) {
        video_clock_ = pts;
    } else {
        pts = video_clock_;
    }
    double frame_delay = av_q2d(video_stream_->time_base);
    frame_delay += frame->repeat_pict * (frame_delay * 0.5);
    video_clock_ += frame_delay;
    return pts;
}

double Player::GetMasterClock() const {
    if (audio_stream_) {
        return audio_clock_;
    }
    return GetVideoClock();
}

double Player::GetVideoClock() const { return video_clock_; }

void Player::CalculateDisplayRect(SDL_Rect* rect, int pic_width, int pic_height,
                                  AVRational pic_sar) {
    float aspect_ratio;
    if (pic_sar.num == 0 || pic_sar.den == 0) {
        aspect_ratio = 0;
    } else {
        aspect_ratio = av_q2d(pic_sar);
    }

    if (aspect_ratio <= 0.0) {
        aspect_ratio = 1.0;
    }
    aspect_ratio *= static_cast<float>(pic_width) / static_cast<float>(pic_height);

    int win_w, win_h;
    SDL_GetWindowSize(window_.get(), &win_w, &win_h);

    int h = win_h;
    int w = static_cast<int>(round(h * aspect_ratio));
    if (w > win_w) {
        w = win_w;
        h = static_cast<int>(round(w / aspect_ratio));
    }

    rect->x = (win_w - w) / 2;
    rect->y = (win_h - h) / 2;
    rect->w = w;
    rect->h = h;
}

void Player::Run() {
    if (!video_stream_ && !audio_stream_) {
        LOG_ERROR("没有找到视频或音频流");
        return;
    }
    ScheduleNextVideoRefresh(40);
    SDL_Event event;
    while (running_.load()) {
        SDL_WaitEvent(&event);
        switch (event.type) {
            case SDL_QUIT:
                running_.store(false);  // TODO: 要有锁 + 条件变量还是 stop_token
                break;
            case kFFRefreshEvent:       //
                VideoRefreshHandler();  // 视频刷新
                break;
            default:
                break;
        }
    }
}

}  // namespace cuteplayer