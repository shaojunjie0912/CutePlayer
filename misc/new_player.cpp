#include <cuteplayer/main.hpp>
#include <cuteplayer/player.hpp>
#include <stdexcept>
#include <algorithm>

namespace cuteplayer {

// =============================================================================
// PacketQueue Implementation
// =============================================================================

PacketQueue::PacketQueue() {
    fifo_ = av_fifo_alloc2(1, sizeof(UniqueAVPacket), AV_FIFO_FLAG_AUTO_GROW);
    if (!fifo_) {
        throw std::runtime_error("Failed to allocate AVFifo for PacketQueue");
    }
}

PacketQueue::~PacketQueue() {
    Flush();
    av_fifo_freep2(&fifo_);
}

bool PacketQueue::Put(UniqueAVPacket packet) {
    std::lock_guard lock(mutex_);
    if (av_fifo_write(fifo_, &packet, 1) < 0) {
        LOG_ERROR("Failed to write packet to fifo");
        return false;
    }
    // unique_ptr's ownership has been transferred to the fifo, so release it.
    packet.release(); 
    packet_count_++;
    cv_.notify_one();
    return true;
}

std::optional<UniqueAVPacket> PacketQueue::Get(bool block, std::stop_token stop_token) {
    std::unique_lock lock(mutex_);
    while (true) {
        if (av_fifo_read(fifo_, &packet_holder, 1) >= 0) {
            UniqueAVPacket packet(packet_holder.pkt);
            packet_count_--;
            return packet;
        }

        if (!block || stop_token.stop_requested()) {
            return std::nullopt;
        }

        cv_.wait(lock, stop_token, [this] { return packet_count_ > 0; });
    }
}

void PacketQueue::Flush() {
    std::lock_guard lock(mutex_);
    while (av_fifo_read(fifo_, &packet_holder, 1) >= 0) {
        av_packet_free(&packet_holder.pkt);
    }
    packet_count_ = 0;
    total_size_ = 0;
}

int PacketQueue::GetSize() const {
    std::lock_guard lock(mutex_);
    return total_size_;
}

// =============================================================================
// FrameQueue Implementation
// =============================================================================

FrameQueue::FrameQueue(int max_size) : max_size_(max_size) {
    for (int i = 0; i < max_size_; ++i) {
        DecodedFrame df;
        df.frame.reset(av_frame_alloc());
        if (!df.frame) {
            throw std::runtime_error("Failed to allocate AVFrame for FrameQueue");
        }
        queue_.push_back(std::move(df));
    }
}

std::optional<DecodedFrame*> FrameQueue::PeekWritable() {
    std::unique_lock lock(mutex_);
    if (cv_not_full_.wait_for(lock, std::chrono::milliseconds(1), [this] { return queue_.size() < max_size_; })) {
         DecodedFrame& back_frame = queue_.back();
         return &back_frame;
    }
    return std::nullopt;
}


void FrameQueue::Push() {
    std::lock_guard lock(mutex_);
    // No actual data push, just notify that a writable frame has been written.
    cv_not_empty_.notify_one();
}

std::optional<const DecodedFrame*> FrameQueue::PeekReadable() const {
    std::unique_lock lock(mutex_);
    if (cv_not_empty_.wait_for(lock, std::chrono::milliseconds(1), [this] { return !queue_.empty(); })) {
        return &queue_.front();
    }
    return std::nullopt;
}

void FrameQueue::Pop() {
    std::lock_guard lock(mutex_);
    if (!queue_.empty()) {
        av_frame_unref(queue_.front().frame.get());
        queue_.pop_front();
        cv_not_full_.notify_one();
    }
}

int FrameQueue::GetSize() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
}


// =============================================================================
// Player Implementation
// =============================================================================

Player::Player(std::string file_path)
    : file_path_(std::move(file_path)),
      video_frame_queue_(kVideoPictureQueueSize) {
    init_sdl();
    open_input_file();
    find_streams();
    
    if (video_stream_idx_ != -1) {
        open_stream_component(video_stream_idx_);
    }
    if (audio_stream_idx_ != -1) {
        open_stream_component(audio_stream_idx_);
    }

    start_threads();
}

Player::~Player() {
    LOG_INFO("Player shutting down...");
    stop_source_.request_stop();
    // jthreads will be joined automatically here.
    SDL_Quit();
    LOG_INFO("Player shutdown complete.");
}

void Player::init_sdl() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        throw std::runtime_error("SDL_Init Error: " + std::string(SDL_GetError()));
    }

    window_.reset(SDL_CreateWindow("CutePlayer Modern C++", SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, kDefaultWidth, kDefaultHeight,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE));
    if (!window_) {
        throw std::runtime_error("SDL_CreateWindow Error: " + std::string(SDL_GetError()));
    }

    renderer_.reset(SDL_CreateRenderer(window_.get(), -1,
                                     SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (!renderer_) {
        throw std::runtime_error("SDL_CreateRenderer Error: " + std::string(SDL_GetError()));
    }
    LOG_INFO("SDL initialized successfully.");
}

void Player::open_input_file() {
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, file_path_.c_str(), nullptr, nullptr) != 0) {
        throw std::runtime_error("Cannot open input file: " + file_path_);
    }
    format_ctx_.reset(fmt_ctx);

    if (avformat_find_stream_info(format_ctx_.get(), nullptr) < 0) {
        throw std::runtime_error("Cannot find stream information");
    }
    av_dump_format(format_ctx_.get(), 0, file_path_.c_str(), 0);
    LOG_INFO("Input file opened and stream info found.");
}

void Player::find_streams() {
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
        throw std::runtime_error("Could not find audio or video stream in the input file.");
    }
}

void Player::open_stream_component(int stream_index) {
    AVCodecParameters* codec_params = format_ctx_->streams[stream_index]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        throw std::runtime_error("Unsupported codec!");
    }

    UniqueAVCodecContext codec_ctx(avcodec_alloc_context3(codec));
    if (!codec_ctx) {
        throw std::runtime_error("Failed to allocate codec context");
    }

    if (avcodec_parameters_to_context(codec_ctx.get(), codec_params) < 0) {
        throw std::runtime_error("Failed to copy codec parameters to context");
    }

    if (avcodec_open2(codec_ctx.get(), codec, nullptr) < 0) {
        throw std::runtime_error("Failed to open codec");
    }

    if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_stream_ = format_ctx_->streams[stream_index];
        video_codec_ctx_ = std::move(codec_ctx);
        frame_timer_ = static_cast<double>(av_gettime()) / 1000000.0;
        LOG_INFO("Video component opened.");
    } else if (codec_params->codec_type == AVMEDIA_TYPE_AUDIO) {
        audio_stream_ = format_ctx_->streams[stream_index];
        audio_codec_ctx_ = std::move(codec_ctx);

        SDL_AudioSpec wanted_spec, actual_spec;
        SDL_memset(&wanted_spec, 0, sizeof(wanted_spec));

        AVChannelLayout out_ch_layout;
        av_channel_layout_default(&out_ch_layout, 2);

        wanted_spec.freq = audio_codec_ctx_->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = out_ch_layout.nb_channels;
        wanted_spec.silence = 0;
        wanted_spec.samples = kSdlAudioBufferSize;
        wanted_spec.callback = audio_callback_wrapper;
        wanted_spec.userdata = this;

        if (SDL_OpenAudio(&wanted_spec, &actual_spec) < 0) {
            throw std::runtime_error("SDL_OpenAudio failed: " + std::string(SDL_GetError()));
        }

        // Setup resampler
        swr_alloc_set_opts2(&swr_ctx_, 
                            &out_ch_layout, AV_SAMPLE_FMT_S16, actual_spec.freq,
                            &audio_codec_ctx_->ch_layout, audio_codec_ctx_->sample_fmt, audio_codec_ctx_->sample_rate,
                            0, nullptr);
        swr_init(swr_ctx_.get());
        LOG_INFO("Audio component opened and SDL audio device started.");
    }
}


void Player::start_threads() {
    read_thread_ = std::jthread(&Player::read_loop, this);
    if (video_stream_) {
        video_decode_thread_ = std::jthread(&Player::video_decode_loop, this);
    }
    // Start audio playback
    if (audio_stream_) {
        SDL_PauseAudio(0);
    }
}

void Player::RunEventLoop() {
    schedule_next_video_refresh(40);
    SDL_Event event;
    while (true) {
        SDL_WaitEvent(&event);
        if (stop_source_.stop_requested()) {
            break;
        }
        switch (event.type) {
            case SDL_QUIT:
                stop_source_.request_stop();
                break;
            case kFFRefreshEvent:
                video_refresh_handler();
                break;
            default:
                break;
        }
    }
}

void Player::read_loop(std::stop_token stop_token) {
    LOG_INFO("Read thread started.");
    UniqueAVPacket packet(av_packet_alloc());

    while (!stop_token.stop_requested()) {
        if (video_packet_queue_.GetSize() > kMaxQueueSize || audio_packet_queue_.GetSize() > kMaxQueueSize) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (av_read_frame(format_ctx_.get(), packet.get()) < 0) {
            // EOF or error
            LOG_INFO("av_read_frame reached EOF or failed.");
            break; 
        }

        UniqueAVPacket packet_copy(av_packet_clone(packet.get()));
        if (packet->stream_index == video_stream_idx_) {
            video_packet_queue_.Put(std::move(packet_copy));
        } else if (packet->stream_index == audio_stream_idx_) {
            audio_packet_queue_.Put(std::move(packet_copy));
        }
        av_packet_unref(packet.get());
    }
    
    // Signal event loop to exit when reading is done.
    // This allows the player to close automatically on video end.
    SDL_Event quit_event;
    quit_event.type = SDL_QUIT;
    SDL_PushEvent(&quit_event);

    LOG_INFO("Read thread finished.");
}

void Player::video_decode_loop(std::stop_token stop_token) {
    LOG_INFO("Video decode thread started.");
    UniqueAVFrame frame(av_frame_alloc());

    while (!stop_token.stop_requested()) {
        auto packet_opt = video_packet_queue_.Get(true, stop_token);
        if (!packet_opt) {
            LOG_INFO("Video decode loop stopping.");
            break;
        }

        if (avcodec_send_packet(video_codec_ctx_.get(), packet_opt->get()) < 0) {
            LOG_ERROR("Error sending a packet for decoding");
            continue;
        }

        while (avcodec_receive_frame(video_codec_ctx_.get(), frame.get()) == 0) {
            double pts = (frame->pts == AV_NOPTS_VALUE) ? 0 : frame->pts * av_q2d(video_stream_->time_base);
            pts = synchronize_video(frame.get(), pts);

            auto writable_frame_opt = video_frame_queue_.PeekWritable();
            if (writable_frame_opt) {
                DecodedFrame* df = *writable_frame_opt;
                av_frame_move_ref(df->frame.get(), frame.get());
                df->pts = pts;
                video_frame_queue_.Push();
            }
             av_frame_unref(frame.get());
        }
    }
    LOG_INFO("Video decode thread finished.");
}


void Player::audio_callback_wrapper(void* userdata, Uint8* stream, int len) {
    static_cast<Player*>(userdata)->audio_callback(stream, len);
}

void Player::audio_callback(Uint8* stream, int len) {
    SDL_memset(stream, 0, len);

    while (len > 0) {
        if (audio_buffer_index_ >= audio_buffer_size_) {
            int decoded_size = decode_audio_frame();
            if (decoded_size <= 0) {
                // Error or EOF, fill with silence
                return;
            }
            audio_buffer_size_ = decoded_size;
            audio_buffer_index_ = 0;
        }

        int len_to_copy = std::min(len, static_cast<int>(audio_buffer_size_ - audio_buffer_index_));
        SDL_memcpy(stream, audio_buffer_.data() + audio_buffer_index_, len_to_copy);
        
        len -= len_to_copy;
        stream += len_to_copy;
        audio_buffer_index_ += len_to_copy;
    }
}

int Player::decode_audio_frame() {
    auto packet_opt = audio_packet_queue_.Get(true, stop_source_.get_token());
    if (!packet_opt) return -1;

    UniqueAVFrame frame(av_frame_alloc());

    if (avcodec_send_packet(audio_codec_ctx_.get(), packet_opt->get()) < 0) return -1;

    while (avcodec_receive_frame(audio_codec_ctx_.get(), frame.get()) == 0) {
        if (frame->pts != AV_NOPTS_VALUE) {
            audio_clock_ = frame->pts * av_q2d(audio_stream_->time_base);
        }
        
        uint8_t** out_data = nullptr;
        int out_linesize;
        int out_samples = swr_get_out_samples(swr_ctx_.get(), frame->nb_samples);
        int out_buf_size = av_samples_get_buffer_size(&out_linesize, audio_codec_ctx_->ch_layout.nb_channels, out_samples, AV_SAMPLE_FMT_S16, 1);

        audio_buffer_.resize(out_buf_size);
        out_data = &audio_buffer_[0];

        int converted_samples = swr_convert(swr_ctx_.get(), &out_data, out_samples,
                                            (const uint8_t**)frame->extended_data, frame->nb_samples);

        av_frame_unref(frame.get());
        return converted_samples * audio_codec_ctx_->ch_layout.nb_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    }
    return -1;
}

uint32_t Player::video_refresh_timer_wrapper(uint32_t interval, void* opaque) {
    SDL_Event event;
    event.type = kFFRefreshEvent;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0; // The timer will not repeat
}

void Player::schedule_next_video_refresh(int delay_ms) {
    SDL_AddTimer(delay_ms, video_refresh_timer_wrapper, this);
}

void Player::video_refresh_handler() {
    if (stop_source_.stop_requested() || !video_stream_) {
        return;
    }
    
    auto frame_opt = video_frame_queue_.PeekReadable();
    if (!frame_opt) {
        schedule_next_video_refresh(1); // No frame, try again quickly
        return;
    }

    const DecodedFrame* current_frame = *frame_opt;
    double pts = current_frame->pts;
    double delay = pts - frame_last_pts_;
    if (delay <= 0 || delay >= 1.0) {
        delay = frame_last_delay_;
    }
    frame_last_delay_ = delay;
    frame_last_pts_ = pts;
    
    double ref_clock = get_master_clock();
    double diff = pts - ref_clock;

    double sync_threshold = std::max(delay, kMinAvSyncThreshold);
    if (std::abs(diff) < kAvNoSyncThreshold) {
        if (diff <= -sync_threshold) { // video is behind
            delay = std::max(0.0, delay + diff);
        } else if (diff >= sync_threshold) { // video is ahead
            delay = delay + diff;
        }
    }
    
    frame_timer_ += delay;
    double actual_delay = frame_timer_ - (static_cast<double>(av_gettime()) / 1000000.0);
    if (actual_delay < 0.010) {
        actual_delay = 0.010;
    }
    
    schedule_next_video_refresh(static_cast<int>(actual_delay * 1000 + 0.5));
    
    render_video_frame();
    video_frame_queue_.Pop();
}

void Player::render_video_frame() {
    auto frame_opt = video_frame_queue_.PeekReadable();
    if (!frame_opt) return;

    const AVFrame* frame = (*frame_opt)->frame.get();

    if (!texture_) {
        texture_.reset(SDL_CreateTexture(renderer_.get(), SDL_PIXELFORMAT_IYUV, 
                                        SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height));
        if (!texture_) {
            LOG_ERROR("Cannot create SDL texture: {}", SDL_GetError());
            return;
        }
    }

    SDL_UpdateYUVTexture(texture_.get(), nullptr,
                         frame->data[0], frame->linesize[0],
                         frame->data[1], frame->linesize[1],
                         frame->data[2], frame->linesize[2]);

    SDL_Rect rect;
    calculate_display_rect(&rect, frame->width, frame->height, frame->sample_aspect_ratio);

    SDL_RenderClear(renderer_.get());
    SDL_RenderCopy(renderer_.get(), texture_.get(), nullptr, &rect);
    SDL_RenderPresent(renderer_.get());
}

double Player::synchronize_video(const AVFrame* frame, double pts) {
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

double Player::get_master_clock() const {
    if (audio_stream_) {
        return audio_clock_;
    }
    return get_video_clock();
}

double Player::get_video_clock() const {
    return video_clock_;
}

void Player::calculate_display_rect(SDL_Rect* rect, int pic_width, int pic_height, AVRational pic_sar) {
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


} // namespace cuteplayer