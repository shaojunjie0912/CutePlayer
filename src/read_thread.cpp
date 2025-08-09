#include <cuteplayer/main.hpp>

int OpenStreamComponent(AVState* video_state, uint32_t stream_index) {
    int ret = -1;

    AVFormatContext* format_context{video_state->format_context_};
    if (stream_index >= format_context->nb_streams) {
        LOG_ERROR("stream_index out of range");
        return -1;
    }
    AVStream* stream{format_context->streams[stream_index]};
    AVCodecParameters* codec_params{stream->codecpar};

    // 查找解码器
    AVCodec const* codec{avcodec_find_decoder(codec_params->codec_id)};
    if (!codec) {
        LOG_ERROR("avcodec_find_decoder failed");
        return -1;
    }

    // 创建编解码器上下文
    AVCodecContext* codec_context{
        avcodec_alloc_context3(codec)};  // HACK: 不能轻易释放, 否则内存泄漏
    if (!codec_context) {
        LOG_ERROR("avcodec_alloc_context3 failed");
        return -1;
    }

    // 拷贝参数到编解码器上下文
    ret = avcodec_parameters_to_context(codec_context, codec_params);
    if (ret < 0) {
        LOG_ERROR("avcodec_parameters_to_context failed");
        return -1;
    }

    // 绑定编解码器和编解码器上下文
    ret = avcodec_open2(codec_context, codec, nullptr);
    if (ret < 0) {
        LOG_ERROR("avcodec_open2 failed");
        return -1;
    }

    // 线程 1: 音频(由 SDL 内部创建)
    if (codec_context->codec_type == AVMEDIA_TYPE_AUDIO) {
        AVChannelLayout ch_layout;
        int sample_rate{codec_context->sample_rate};
        ret = av_channel_layout_copy(&ch_layout, &codec_context->ch_layout);
        if (ret < 0) {
            LOG_ERROR("av_channel_layout_copy failed");
            return -1;
        }
        // 打开扬声器
        ret = OpenAudio(video_state, &ch_layout, sample_rate);
        if (ret < 0) {
            LOG_ERROR("OpenAudio failed");
            return -1;
        }
        video_state->audio_stream_ = stream;
        video_state->audio_stream_idx_ = stream_index;
        video_state->audio_codec_context_ = codec_context;

        // 开始播放声音
        SDL_PauseAudio(0);

    }
    // 线程 2: 视频
    else if (codec_context->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_state->video_stream_idx_ = stream_index;
        video_state->video_stream_ = stream;
        video_state->video_codec_context_ = codec_context;  // 待办: 为什么音频编码器上下文没有存?

        // 音视频同步相关字段
        video_state->frame_timer_ = (double)av_gettime() / 1000000.0;
        video_state->frame_last_delay_ = 40e-3;
        video_state->video_current_pts_ = av_gettime();

        video_state->decode_tid_ = SDL_CreateThread(DecodeThread, "decode_thread", video_state);
    }

    // 注意: 正常退出就不需要释放内存(因为赋值出去了)
    // 但是异常退出呢? 视频中使用 goto 的方式释放内存, 那现代C++呢?
    return 0;
}

AVState* OpenStream(std::string const& file_name) {
    int ret{0};

    AVState* video_state = new AVState();

    video_state->file_name_ = file_name;

    // 初始化视频包队列
    ret = InitPacketQueue(&video_state->video_packet_queue_);
    if (ret < 0) {
        LOG_ERROR("Init Video PacketQueue failed");
        return nullptr;
    }

    // 初始化音频包队列
    ret = InitPacketQueue(&video_state->audio_packet_queue_);
    if (ret < 0) {
        LOG_ERROR("Init Audio PacketQueue failed");
        return nullptr;
    }

    // 初始化视频帧队列
    ret = InitFrameQueue(&video_state->video_frame_queue_, &video_state->video_packet_queue_,
                         kVideoPictureQueueSize, 1);
    if (ret < 0) {
        LOG_ERROR("Init Video FrameQueue failed");
        return nullptr;
    }

    // 开启读线程
    video_state->read_tid_ = SDL_CreateThread(ReadThread, "ReadThread", video_state);
    if (!video_state->read_tid_) {
        LOG_ERROR("SDL_CreateThread failed");
        return nullptr;
    }

    RefreshSchedule(video_state, 40);  // HACK: 注释后没有视频了

    return video_state;
}

int ReadThread(void* arg) {
    int ret{-1};

    AVState* video_state = static_cast<AVState*>(arg);

    AVFormatContext* format_context{nullptr};
    ret = avformat_open_input(&format_context, video_state->file_name_.c_str(), nullptr, nullptr);
    if (ret < 0) {
        LOG_ERROR("avformat_open_input failed");
        return -1;
    }

    video_state->format_context_ = format_context;  // 注意: 不能close, 否则悬空指针

    ret = avformat_find_stream_info(format_context, nullptr);
    if (ret < 0) {
        LOG_ERROR("avformat_find_stream_info failed");
        return -1;
    }

    // 查找音频流和视频流
    for (uint32_t i{0}; i < format_context->nb_streams; ++i) {
        AVStream* stream = format_context->streams[i];
        AVCodecParameters* codecpar = stream->codecpar;
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_state->video_stream_idx_ = i;
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            video_state->audio_stream_idx_ = i;
        }
    }
    // 打开视频流
    // 重设视频窗口大小(这样最好, 防止分辨率不对)
    AVStream* stream{format_context->streams[video_state->video_stream_idx_]};
    AVCodecParameters* codec_params{stream->codecpar};
    AVRational sar{av_guess_sample_aspect_ratio(format_context, stream, nullptr)};
    if (codec_params->width) {
        // 待办: 设置默认窗口大小
        SetDefaultWindowSize(codec_params->width, codec_params->height, sar);
    }

    // 视频解码线程
    OpenStreamComponent(video_state, video_state->video_stream_idx_);
    // 音频解码线程
    OpenStreamComponent(video_state, video_state->audio_stream_idx_);

    AVPacket* packet{av_packet_alloc()};

    while (true) {
        // 用户退出
        if (video_state->quit_) {
            return -1;
        }

        // 限制队列大小
        if (video_state->audio_packet_queue_.size_ > kMaxQueueSize ||
            video_state->video_packet_queue_.size_ > kMaxQueueSize) {
            SDL_Delay(10);  // 如果队列满了, 等待 10 ms, 等消费者消费
            continue;
        }

        // 读取包
        ret = av_read_frame(format_context, packet);
        if (ret < 0) {
            if (format_context->pb->error == 0) {
                SDL_Delay(100);  // 没有错误, 等待用户输入
                continue;
            } else {
                break;
            }
        }

        // 保存包到队列
        if (packet->stream_index == video_state->video_stream_idx_) {
            PutPacketQueue(&video_state->video_packet_queue_, packet);  // 保存视频包
        } else if (packet->stream_index == video_state->audio_stream_idx_) {
            PutPacketQueue(&video_state->audio_packet_queue_, packet);  // 保存音频包
        } else {
            av_packet_unref(packet);  // 既不是音频流, 也不是视频流, 释放包
        }
    }
    // 等待用户关闭窗口(接收到一个 quit 消息)
    while (!video_state->quit_) {
        SDL_Delay(100);
    }

    // 释放资源
    if (packet) {
        av_packet_free(&packet);  // 注意: 这里之所以正常释放是因为保存到队列是"复制"
    }
    return 0;
}