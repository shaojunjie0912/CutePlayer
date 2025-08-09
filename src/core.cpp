#include <cuteplayer/main.hpp>

// ================== core ==================

// peek: 窥视(用于 FrameQueue)
// get: 获取(用于 PacketQueue)

int InitPacketQueue(PacketQueue *q) {
    // 手动初始化各成员，避免使用 memset 清除非平凡类型
    q->pkt_list_ = nullptr;
    q->nb_packets_ = 0;
    q->size_ = 0;
    q->duration_ = 0;
    q->pkt_list_ = av_fifo_alloc2(1, sizeof(MyAVPacketList), AV_FIFO_FLAG_AUTO_GROW);
    if (!q->pkt_list_) {
        return AVERROR(ENOMEM);
    }
    return 0;
}

int PutPacketQueueInternal(PacketQueue *q, AVPacket *pkt) {
    MyAVPacketList pkt1;
    int ret;

    pkt1.pkt = pkt;

    // 写入队列
    ret = av_fifo_write(q->pkt_list_, &pkt1, 1);
    if (ret < 0) {
        return ret;
    }
    ++q->nb_packets_;
    q->size_ += pkt1.pkt->size + sizeof(pkt1);
    q->duration_ += pkt1.pkt->duration;
    q->cv_.notify_one();  // 通知等待的线程
    return 0;
}

int PutPacketQueue(PacketQueue *q, AVPacket *pkt) {
    std::unique_lock lk{q->mtx_};
    int ret;
    AVPacket *pkt1{av_packet_alloc()};  // HACK: 分配新内存
    if (!pkt1) {
        av_packet_unref(pkt);
        return -1;
    }
    av_packet_move_ref(pkt1, pkt);

    ret = PutPacketQueueInternal(q, pkt1);

    if (ret < 0) {
        av_packet_free(&pkt1);
    }

    return ret;
}

int GetPacketQueue(PacketQueue *q, AVPacket *pkt, int block) {
    std::unique_lock lk{q->mtx_};
    MyAVPacketList pkt1;
    int ret;
    for (;;) {
        if (av_fifo_read(q->pkt_list_, &pkt1, 1) >= 0) {
            --q->nb_packets_;
            q->size_ -= pkt1.pkt->size + sizeof(pkt1);
            q->duration_ -= pkt1.pkt->duration;
            av_packet_move_ref(pkt, pkt1.pkt);
            av_packet_free(&pkt1.pkt);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            q->cv_.wait(lk);
        }
    }
    return ret;
}

void FlushPacketQueue(PacketQueue *q) {
    std::unique_lock lk{q->mtx_};
    MyAVPacketList pkt1;
    while (av_fifo_read(q->pkt_list_, &pkt1, 1) >= 0) {
        av_packet_free(&pkt1.pkt);
    }
    q->nb_packets_ = 0;
    q->size_ = 0;
    q->duration_ = 0;
}

void DestoryPacketQueue(PacketQueue *q) {
    FlushPacketQueue(q);
    av_fifo_freep2(&q->pkt_list_);
}

int InitFrameQueue(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last) {
    int i;
    // 手动初始化各成员，避免使用 memset 清除非平凡类型
    f->rindex_ = 0;
    f->windex_ = 0;
    f->size_ = 0;
    f->rindex_shown_ = 0;
    f->pktq_ = pktq;
    f->max_size_ = std::min(max_size, kFrameQueueSize);
    f->keep_last_ = !!keep_last;

    // 初始化队列中的每个 Frame
    for (i = 0; i < kFrameQueueSize; i++) {
        f->queue_[i] = {};  // 零初始化每个 Frame
    }

    for (i = 0; i < f->max_size_; i++) {
        // 为 FrameQueue 中的 max_size 个 Frame 分配内存
        if (!(f->queue_[i].frame_ = av_frame_alloc())) {
            return AVERROR(ENOMEM);
        }
    }
    return 0;
}

// Frame *FrameQueuePeekReadable(FrameQueue *f) {
//     std::unique_lock lk{f->mtx_};
//     f->cv_notempty_.wait(lk, [&] { return f->size_ - f->rindex_shown_ > 0; });
//     return &f->queue_[(f->rindex_ + f->rindex_shown_) % f->max_size_];
// }

// 窥视一个可以写的 Frame，此函数可能会阻塞。
Frame *PeekWritableFrameQueue(FrameQueue *f) {
    std::unique_lock lk{f->mtx_};
    f->cv_notfull_.wait(lk, [&] { return f->size_ < f->max_size_; });
    return &f->queue_[f->windex_];
}

// 偏移读索引 rindex
// HACK: 第一次 Peek 读的时候 rindex + rindex_shown = 0 + 0
// 然后单独递增 rindex_shown 并 return
// 下一次 Peek 读的时候 rindex + rindex_shown = 0 + 1
void MoveReadIndex(FrameQueue *f) {
    std::unique_lock lk{f->mtx_};
    if (f->keep_last_ && !f->rindex_shown_) {
        f->rindex_shown_ = 1;
        return;
    }
    av_frame_unref(f->queue_[f->rindex_].frame_);
    if (++f->rindex_ == f->max_size_) {
        f->rindex_ = 0;
    }
    --f->size_;
    f->cv_notfull_.notify_one();
}

// 偏移写索引 windex
void MoveWriteIndex(FrameQueue *f) {
    std::unique_lock lk{f->mtx_};
    if (++f->windex_ == f->max_size_) {
        f->windex_ = 0;
    }
    ++f->size_;
    f->cv_notempty_.notify_one();
}

// 获取当前可读取的帧，而不改变队列状态。
// 渲染线程在渲染当前帧时使用，不会修改队列状态。
Frame *PeekFrameQueue(FrameQueue *f) {
    // HACK: 读取索引 + 读取索引偏移
    std::unique_lock lk{f->mtx_};
    return &f->queue_[(f->rindex_ + f->rindex_shown_) % f->max_size_];
}

// ================== common ==================

uint32_t MyRefreshTimerCallback(uint32_t, void *opaque) {
    SDL_Event event;
    event.type = kFFRefreshEvent;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);  // 插入 SDL 事件队列
    return 0;
}

void RefreshSchedule(AVState *video_state, int delay) {
    // 计算延迟
    SDL_AddTimer(delay, MyRefreshTimerCallback, video_state);
}

void CalculateDisplayRect(SDL_Rect *rect, int screen_x_left, int screen_y_top, int screen_width,
                          int screen_height, int picture_width, int picture_height,
                          AVRational picture_sar) {
    // 注意: picture_sar 代表 sample aspect ratio 图片的像素宽高比(即图像每个像素的宽高比)
    // 不同于 DAR(display aspect ratio) 显示的宽高比
    AVRational aspect_ratio = picture_sar;
    int64_t width, height, x, y;

    // 如果 pic_sar 为零或负值（即无效值），则将 aspect_ratio 设置为 1:1，表示无畸变的方形像素。
    if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0) {
        aspect_ratio = av_make_q(1, 1);
    }

    // 计算显示的宽高比, 根据图像原始尺寸和像素宽高比计算
    // (显示的宽高比 = 图像的宽高比 * 图像的宽度 / 图像的高度)
    aspect_ratio = av_mul_q(aspect_ratio, av_make_q(picture_width, picture_height));

    // 计算显示的宽高
    height = screen_height;
    width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1;
    if (width > screen_width) {
        width = screen_width;
        height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1;
    }

    // 计算显示的位置
    x = (screen_width - width) / 2;
    y = (screen_height - height) / 2;

    rect->x = screen_x_left + x;
    rect->y = screen_y_top + y;

    rect->w = FFMAX((int)width, 1);
    rect->h = FFMAX((int)height, 1);
}

void SetDefaultWindowSize(int width, int height, AVRational sar) {
    SDL_Rect rect;
    // int max_width = screen_width ? screen_width : INT_MAX;
    // int max_height = screen_height ? screen_height : INT_MAX;
    // if (max_width == INT_MAX && max_height == INT_MAX) max_height = height;
    int max_width = kScreenWidth;
    int max_height = kScreenHeight;
    CalculateDisplayRect(&rect, 0, 0, max_width, max_height, width, height, sar);
    // default_width = rect.w;
    // default_height = rect.h;
}