#include <condition_variable>
#include <cuteplayer/player.hpp>
#include <stdexcept>

// TODO: 没有 std::stop_token 就用 stop 标志位
// NOTE: 一个 AVPacket 可能对应一个或多个 AVFrame
// 但也可能多个 AVPacket 才可以解码出一个 AVFrame (比如: 帧间依赖)

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
      audio_frame_(av_frame_alloc()) {
    // TODO:
    // OpenStreamComponent(video_stream_idx_);
    // OpenStreamComponent(audio_stream_idx_);
    // StartThreads();
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
        if (codec_context->sample_fmt != AV_SAMPLE_FMT_S16) {
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

void Player::VideoDecodeLoop() { LOG_INFO(""); }

// 返回的是音频帧的数量
int Player::DecodeAudioFrame() {
    int ret{0};
    while (true) {
        // TODO: TryPop?
        auto packet{audio_packet_queue_.TryPop()};
        if (!packet) {
            // TODO: 音频包队列为空 应该返回 0 吗? 静音
            // 是否应该把 receive 放上面
            return 0;
        }

        // avcodec_send_packet: 异步发送一个 AVPacket 到解码器(解码器内部维护一个 AVPacket 队列)
        ret = avcodec_send_packet(audio_codec_ctx_.get(), packet->get());
        // TODO: 这里直接unref对下面有影响吗?
        av_packet_unref(packet->get());  // 由于解码器复制了, 所以我们自己的引用计数减一, 不需要了
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                // 这是一个状态信号，不是错误。我们记录一下，然后正常继续到 receive 环节。
                // 解码器输入缓冲区满了, 需要取走一些 AVFrame, 再重新把这个 packet 送进去
                LOG_DEBUG("avcodec_send_packet() 需要 receive, ret: EAGAIN");
            } else if (ret == AVERROR_EOF) {
                // 发送流已结束，这也是正常状态。
                // 之前发送了一个 null packet 冲刷解码器
                LOG_INFO("avcodec_send_packet() 收到 EOF");
            } else {
                // 真正的致命错误！立即停止。
                LOG_ERROR("avcodec_send_packet() 发生错误: {}", av_err2str(ret));
                return ret;
            }
        }

        // 循环调用 avcodec_receive_frame 以获取所有可能产生的帧 (0,1,...)
        while (ret >= 0) {
            ret = avcodec_receive_frame(audio_codec_ctx_.get(), audio_frame_.get());
            if (ret == AVERROR(EAGAIN)) {
                // EAGAIN: 需要更多 packet 才能输出 frame, 退出内层循环去读下一个 packet
                break;
            } else if (ret == AVERROR_EOF) {
                // EOF: 解码器已完全刷新，没有更多 frame 了
                return -1;  // TODO: 不需要考虑已经生成的帧数量吗
            } else if (ret < 0) {
                // 真正的解码错误
                LOG_ERROR("avcodec_receive_frame 发生错误");
                return ret;
            }
            // 正常情况
            LOG_INFO("成功解码一帧, pts: {}, dts: {}", audio_frame_->pts, audio_frame_->pkt_dts);
            int frame_cnt{0};
            auto in = static_cast<uint8_t* const*>(audio_frame_.get()->extended_data);
            int in_count = audio_frame_.get()->nb_samples;
            // uint8_t** out = &audio_buffer_;
            // 比输入多 256 是一个安全余量
            // 因为重采样过程中可能会有轻微的延迟和缓存, 导致输出样本数略多于输入
            int out_count = audio_frame_.get()->nb_samples + 256;

            // 重采样后输出缓冲区大小
            // = 2 * 2 * audio_frame_.nb_samples
            int out_size =
                av_samples_get_buffer_size(nullptr, audio_frame_.get()->ch_layout.nb_channels,
                                           out_count, AV_SAMPLE_FMT_S16, 0);
            // 重新分配 audio_buffer_ 内存
            audio_buffer_.resize(out_size);
            auto out = &audio_buffer_[0];

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
        // TODO: 为什么用 audio_buf_size_ 而不是直接 audio_buffer_.size()?
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

}  // namespace cuteplayer