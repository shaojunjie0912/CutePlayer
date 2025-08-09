#include <cuteplayer/main.hpp>

extern SDL_Window* window;
extern SDL_Renderer* renderer;

int OpenVideo(AVState* video_state) {
    SDL_SetWindowTitle(window, video_state->file_name_.c_str());

    SDL_SetWindowSize(window, kScreenWidth, kScreenHeight);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    video_state->width_ = kScreenWidth;
    video_state->height_ = kScreenHeight;

    return 0;
}

void DisplayVideo(AVState* video_state) {
    if (video_state->width_ == 0) {
        OpenVideo(video_state);
    }
    Frame* vp = PeekFrameQueue(&video_state->video_frame_queue_);

    AVFrame* frame = vp->frame_;

    if (!video_state->texture_) {
        int width = frame->width;
        int height = frame->height;

        uint32_t pix_format = SDL_PIXELFORMAT_IYUV;
        video_state->texture_ =
            SDL_CreateTexture(renderer, pix_format, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!video_state->texture_) {
            LOG_ERROR("SDL_CreateTexture failed");
            return;
        }
    }

    // 计算显示的位置
    SDL_Rect rect;
    CalculateDisplayRect(&rect, video_state->x_left_, video_state->y_top_, video_state->width_,
                         video_state->height_, vp->width_, vp->height_, vp->sar_);

    // 渲染
    SDL_UpdateYUVTexture(video_state->texture_, nullptr, frame->data[0], frame->linesize[0],
                         frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, video_state->texture_, nullptr, &rect);
    SDL_RenderPresent(renderer);

    // 释放视频帧
    MoveReadIndex(&video_state->video_frame_queue_);
}

void VideoRefreshTimer(void* user_data) {
    AVState* video_state = static_cast<AVState*>(user_data);
    Frame* vp{nullptr};

    double actual_delay, delay, sync_threshold, ref_clock, diff;

    if (video_state->video_stream_) {                      // 如果存在视频流
        if (video_state->video_frame_queue_.size_ == 0) {  // 如果视频帧队列为空
            RefreshSchedule(video_state, 1);               // 快速刷新直到发现有数据
        } else {
            vp = PeekFrameQueue(&video_state->video_frame_queue_);
            video_state->video_current_pts_ = vp->pts_;
            video_state->video_current_pts_time_ = av_gettime();
            if (video_state->frame_last_pts_ == 0) {
                delay = 0;
            } else {
                // 上一次的 pts
                delay = vp->pts_ - video_state->frame_last_pts_;
            }

            if (delay <= 0 || delay >= 1.0) {
                // 如果是不正确的 delay, 使用上一次的 delay
                delay = video_state->frame_last_delay_;
            }
            // 保存供下次使用
            video_state->frame_last_delay_ = delay;
            video_state->frame_last_pts_ = vp->pts_;

            // 更新 delay 同步到音频
            // ref_clock = GetMasterClock(video_state);  // 获取主时钟(这里就是音频时钟)
            ref_clock = video_state->audio_clock_;  // 注意: 我直接写死了
            diff = vp->pts_ - ref_clock;

            // 跳过或重复帧。考虑延迟因素
            // FFPlay 仍然不知道"这是否是最佳猜测。"
            sync_threshold = (delay > kMaxAvSyncThreshold) ? delay : kMaxAvSyncThreshold;
            if (fabs(diff) < kAvNoSyncThreshold) {
                if (diff <= -sync_threshold) {        // diff 小于负阈值, 视频慢了
                    delay = 0;                        // 不要延迟，立即播放
                } else if (diff >= sync_threshold) {  // diff 大于阈值, 视频快了
                    delay = 2 * delay;                // 延迟
                }
            }
            video_state->frame_timer_ += delay;  // 更新视频时钟
            // 计算实际延迟
            actual_delay = video_state->frame_timer_ - (av_gettime() / 1000000.0);
            if (actual_delay < 0.010) {
                actual_delay = 0.010;
            }

            RefreshSchedule(video_state, (int)(actual_delay * 1000 + 0.5));

            DisplayVideo(video_state);
        }
    } else {
        RefreshSchedule(video_state, 100);
    }
}

void SdlEventLoop(AVState* video_state) {
    SDL_Event event;
    while (true) {
        SDL_WaitEvent(&event);
        switch (event.type) {
            case SDL_QUIT:
                video_state->quit_ = true;
                SDL_Quit();
                return;
            case kFFRefreshEvent:
                // 注意: 这里是视频刷新
                VideoRefreshTimer(event.user.data1);
                break;
            default:
                break;
        }
    }
}

int QueuePicture(AVState* video_state, AVFrame* src_frame, double pts, double duration,
                 int64_t pos) {
    Frame* vp;
    if (!(vp = PeekWritableFrameQueue(&video_state->video_frame_queue_))) {
        LOG_ERROR("PeekWritableFrameQueue failed");
        return -1;
    }
    vp->sar_ = src_frame->sample_aspect_ratio;
    vp->width_ = src_frame->width;
    vp->height_ = src_frame->height;
    vp->format_ = src_frame->format;

    vp->pts_ = pts;
    vp->duration_ = duration;
    vp->pos_ = pos;

    SetDefaultWindowSize(vp->width_, vp->height_, vp->sar_);

    av_frame_move_ref(vp->frame_, src_frame);
    MoveWriteIndex(&video_state->video_frame_queue_);
    return 0;
}

double SyschronizeVideo(AVState* video_state, AVFrame* frame, double pts) {
    double frame_delay;

    if (pts != 0) {
        // 如果有 pts 则设置 video_clock = pts
        video_state->video_clock_ = pts;  // pts from codec
    } else {
        // 如果没有 pts 则设置 pts = video_clock
        pts = video_state->video_clock_;  // use previous frame pts
    }

    //  更新视频时钟
    frame_delay = av_q2d(video_state->video_stream_->time_base);  // 时间基 -> 秒
    // 如果我们在重复一帧，相应调整时钟
    frame_delay += frame->repeat_pict * (frame_delay * 0.5);
    video_state->video_clock_ += frame_delay;

    return pts;
}

int DecodeThread(void* arg) {
    int ret{-1};

    double pts;
    double duration;

    AVState* video_state = static_cast<AVState*>(arg);
    AVFrame* video_frame = av_frame_alloc();  // 解码后的视频帧

    AVRational time_base = video_state->video_stream_->time_base;
    AVRational frame_rate = video_state->video_stream_->avg_frame_rate;

    while (true) {
        if (video_state->quit_) {
            break;
        }

        // 非阻塞读取
        ret = GetPacketQueue(&video_state->video_packet_queue_, &video_state->video_packet_, 0);
        if (ret <= 0) {
            // 意味着我们停止获取包
            LOG_DEBUG("Video delay 10 ms");
            SDL_Delay(10);  // 没当读不到数据就等 10 ms 再看
            continue;
        }

        ret = avcodec_send_packet(video_state->video_codec_context_, &video_state->video_packet_);
        av_packet_unref(&video_state->video_packet_);  // 清空引用计数(因为解码器内部会拷贝一份)
        if (ret < 0) {
            LOG_ERROR("avcodec_send_packet failed");
            return -1;
        }
        while (ret >= 0) {
            ret = avcodec_receive_frame(video_state->video_codec_context_, video_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                LOG_ERROR("avcodec_receive_frame failed");
                return -1;
            }

            // 注意: 如果不做音视频同步的话，这里直接显示就行了
            // DisplayVideo(video_state);

            // ================== 音视频同步 ==================
            // 计算当前帧的时长
            AVRational rational{frame_rate.den, frame_rate.num};
            duration = (frame_rate.num && frame_rate.den ? av_q2d(rational) : 0);
            pts = (video_frame->pts == AV_NOPTS_VALUE) ? NAN : video_frame->pts * av_q2d(time_base);
            pts = SyschronizeVideo(video_state, video_frame, pts);

            // 插入到视频帧队列
            QueuePicture(video_state, video_frame, pts, duration, AV_NOPTS_VALUE);

            // 解引用
            av_frame_unref(video_frame);
        }
    }
    return 0;
}