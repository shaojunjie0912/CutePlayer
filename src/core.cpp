#include <cuteplayer/core.hpp>
#include <cuteplayer/logger.hpp>

namespace cuteplayer {

// =============================================================================
// PacketQueue 实现
// =============================================================================

bool PacketQueue::Push(UniqueAVPacket packet) {
    std::unique_lock lk{mtx_};
    if (closed_) {
        return false;
    }
    curr_data_bytes_ += packet->size;
    duration_ += packet->duration;
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
    duration_ -= packet->duration;
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
    duration_ -= packet->duration;
    return packet;
}

void PacketQueue::Clear() {
    std::unique_lock lk{mtx_};
    std::queue<UniqueAVPacket>{}.swap(queue_);
    curr_data_bytes_ = 0;
    duration_ = 0;
}

void PacketQueue::Close() {
    std::unique_lock lk{mtx_};
    if (closed_) {
        return;
    }
    closed_ = true;
    cv_can_pop_.notify_all();
}

std::size_t PacketQueue::GetTotalDataSize() const {
    std::lock_guard lk{mtx_};
    return curr_data_bytes_;
}

// =============================================================================
// FrameQueue 实现
// =============================================================================

FrameQueue::FrameQueue(int max_size, bool keep_last_frame)
    : max_size_(std::min(16, max_size)),  // TODO: 与16比较取小的那个
      decoded_frames_(max_size_),
      keep_last_frame_(keep_last_frame) {
    for (auto& decoded_frame : decoded_frames_) {
        decoded_frame.frame_.reset(av_frame_alloc());
        if (!decoded_frame.frame_) {
            throw std::runtime_error("分配 AVFrame 失败!");
        }
    }
}

// TODO: PeekWritable + MoveWriteIndex 是否应该合并?
DecodedFrame* FrameQueue::PeekWritable() {
    std::unique_lock lk{mtx_};
    cv_can_write_.wait(lk, [this] { return size_ < max_size_; });
    return &decoded_frames_[windex_];
}

void FrameQueue::MoveWriteIndex() {
    std::unique_lock lk{mtx_};
    if (++windex_ == max_size_) {
        windex_ = 0;
    }
    ++size_;
    cv_can_read_.notify_one();
}

// TODO: PeekReadable + MoveReadIndex 是否应该合并?
DecodedFrame* FrameQueue::PeekReadable() {
    std::unique_lock lk{mtx_};
    cv_can_read_.wait(lk, [this] { return size_ > 0; });
    return &decoded_frames_[rindex_];
}

DecodedFrame* FrameQueue::PeekLastReadable() {
    std::unique_lock lk{mtx_};
    cv_can_read_.wait(lk, [this] { return size_ > 1; });
    return &decoded_frames_[(rindex_ - 1 + max_size_) % max_size_];
}

// TODO: PeekReadable 后调用, 是否可以合并?
// 偏移读索引 rindex
// HACK: 第一次 Peek 读的时候 rindex + rindex_shown = 0 + 0
// 然后单独递增 rindex_shown 并 return
// 下一次 Peek 读的时候 rindex + rindex_shown = 0 + 1
// NOTE: 这个逻辑对每一帧都渲染两次?
void FrameQueue::MoveReadIndex() {
    std::unique_lock lk{mtx_};
    if (keep_last_frame_ && rindex_shown_ == 0) {
        rindex_shown_ = 1;  // 标记为: 已显示但保留
        return;
    }
    av_frame_unref(decoded_frames_[rindex_].frame_.get());
    if (++rindex_ == max_size_) {
        rindex_ = 0;
    }
    rindex_shown_ = 0;  // 重置
    --size_;
    cv_can_write_.notify_one();
}

std::size_t FrameQueue::GetSize() const {
    std::lock_guard lk{mtx_};
    return size_;
}

}  // namespace cuteplayer