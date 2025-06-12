#include <cuteplayer/core.hpp>

// peek: 偷看(用于 FrameQueue)
// get: 获取(用于 PacketQueue)

int InitPacketQueue(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
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

    // 写进队列
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
    memset(f, 0, sizeof(FrameQueue));
    f->pktq_ = pktq;
    f->max_size_ = FFMIN(max_size, kFrameQueueSize);
    f->keep_last_ = !!keep_last;
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

// peek 出一个可以写的 Frame，此函数可能会阻塞。
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
