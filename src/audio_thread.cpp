#include <cuteplayer/main.hpp>

int AudioDecodeFrame(VideoState* video_state) {
    int ret{-1};
    while (true) {
        // 从队列中读取数据
        ret = GetPacketQueue(&video_state->audio_packet_queue_, &video_state->audio_packet_, 0);
        if (ret <= 0) {
            av_log(nullptr, AV_LOG_ERROR, "GetPacketQueue failed\n");
            break;
        }
        ret = avcodec_send_packet(video_state->audio_codec_context_, &video_state->audio_packet_);
        av_packet_unref(&video_state->audio_packet_);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "avcodec_send_packet failed\n");
            return -1;
        }
        while (ret >= 0) {
            ret = avcodec_receive_frame(video_state->audio_codec_context_,
                                        &video_state->audio_frame_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                av_log(nullptr, AV_LOG_ERROR, "avcodec_receive_frame failed\n");
                return -1;
            }
            if (!video_state->audio_swr_context_) {
                AVChannelLayout in_ch_layout, out_ch_layout;
                av_channel_layout_copy(&in_ch_layout,
                                       &video_state->audio_codec_context_->ch_layout);
                av_channel_layout_copy(&out_ch_layout, &in_ch_layout);

                // 重采样
                if (video_state->audio_codec_context_->sample_fmt != AV_SAMPLE_FMT_S16) {
                    swr_alloc_set_opts2(
                        &video_state->audio_swr_context_, &out_ch_layout, AV_SAMPLE_FMT_S16,
                        video_state->audio_codec_context_->sample_rate, &in_ch_layout,
                        video_state->audio_codec_context_->sample_fmt,
                        video_state->audio_codec_context_->sample_rate, 0, nullptr);
                    swr_init(video_state->audio_swr_context_);
                }
            }
            int data_size{0};
            if (video_state->audio_swr_context_) {
                uint8_t* const* in =
                    static_cast<uint8_t* const*>(video_state->audio_frame_.extended_data);
                int in_count = video_state->audio_frame_.nb_samples;
                uint8_t** out = &video_state->audio_buffer_;  // TODO: audio_buffer_ 内存泄漏问题
                int out_count = video_state->audio_frame_.nb_samples + 256;

                // 重采样后输出缓冲区大小
                // = 2 * 2 * video_state->audio_frame_.nb_samples
                int out_size = av_samples_get_buffer_size(
                    nullptr, video_state->audio_frame_.ch_layout.nb_channels, out_count,
                    AV_SAMPLE_FMT_S16, 0);
                // 重新分配 audio_buffer_ 内存
                av_fast_malloc(&video_state->audio_buffer_, &video_state->audio_buffer_size_,
                               out_size);

                // 重采样 -> 返回每个通道的样本数
                int nb_ch_samples =
                    swr_convert(video_state->audio_swr_context_, out, out_count, in, in_count);
                data_size = nb_ch_samples * video_state->audio_frame_.ch_layout.nb_channels *
                            av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
            }
            // HACK: 关键 计算音频时钟
            if (!isnan(video_state->audio_frame_.pts)) {
                video_state->audio_clock_ =
                    video_state->audio_frame_.pts + (double)video_state->audio_frame_.nb_samples /
                                                        video_state->audio_frame_.sample_rate;
            } else {
                video_state->audio_clock_ = NAN;
            }
            av_frame_unref(&video_state->audio_frame_);
            return data_size;
        }
    }
    return 0;
}

/**
 * @brief 音频回调函数(由 SDL 创建线程)
 * @param userdata 用户数据
 * @param stream 音频数据流(NOTE: 音频设备从该流中获取数据 🧀)
 * @param len 需要填充的数据长度
 */
void MyAudioCallback(void* userdata, uint8_t* stream, int len) {
    VideoState* video_state{(VideoState*)userdata};
    int remain_len = 0;
    while (len > 0) {
        // 缓冲区没有数据了
        if (video_state->audio_buffer_index_ >= video_state->audio_buffer_size_) {
            // 已经发送我们所有的数据，获取更多
            int decoded_audio_size = AudioDecodeFrame(video_state);
            if (decoded_audio_size < 0) {  // 如果出错了，输出静音
                video_state->audio_buffer_ = nullptr;
                video_state->audio_buffer_size_ = kSdlAudioBufferSize;
            } else {
                video_state->audio_buffer_size_ = decoded_audio_size;
            }
            video_state->audio_buffer_index_ = 0;  // 重置索引, 下次从头读
        }
        remain_len = video_state->audio_buffer_size_ - video_state->audio_buffer_index_;
        if (remain_len > len) {  // 如果剩余数据大于需要填充的数据长度 len
            remain_len = len;    // 只填充 len 长度的数据
        }
        if (video_state->audio_buffer_) {
            // 如果音频缓冲区有数据，拷贝到 stream 中
            memcpy(stream, video_state->audio_buffer_ + video_state->audio_buffer_index_,
                   remain_len);
        } else {
            memset(stream, 0, remain_len);  // 静音
        }
        len -= remain_len;
        stream += remain_len;
        video_state->audio_buffer_index_ += remain_len;
    }
}

int OpenAudio(void* opaque, AVChannelLayout* wanted_channel_layout, int wanted_sample_rate) {
    int wanted_nb_channels{wanted_channel_layout->nb_channels};

    // 设置音频参数
    SDL_AudioSpec wanted_spec{};
    wanted_spec.freq = wanted_sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = (uint8_t)wanted_nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = kSdlAudioBufferSize;
    wanted_spec.callback = MyAudioCallback;
    wanted_spec.userdata = opaque;

    av_log(nullptr, AV_LOG_INFO, "wanted spec: channels: %d, sample_fmt: %d, sample_rate:%d\n",
           wanted_nb_channels, AUDIO_S16SYS, wanted_sample_rate);

    SDL_AudioSpec spec;
    int ret = SDL_OpenAudio(&wanted_spec, &spec);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "SDL_OpenAudio failed\n");
        return -1;
    }
    return spec.size;
}