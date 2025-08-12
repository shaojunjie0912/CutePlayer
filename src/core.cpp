#include <avplayer/core.hpp>
#include <avplayer/logger.hpp>

namespace avplayer {

// =============================================================================
// PacketQueue 实现
// =============================================================================

bool PacketQueue::Push(UniqueAVPacket packet) {
    std::unique_lock lk{mtx_};
    cv_can_push_.wait(lk, [this] { return closed_ || curr_data_bytes_ < max_data_bytes_; });
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
    if (queue_.empty()) {
        return std::nullopt;
    }
    auto packet{std::move(queue_.front())};
    queue_.pop();
    curr_data_bytes_ -= packet->size;
    duration_ -= packet->duration;
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
    duration_ -= packet->duration;
    cv_can_push_.notify_one();
    return packet;
}

void PacketQueue::Clear() {
    std::unique_lock lk{mtx_};
    std::queue<UniqueAVPacket>{}.swap(queue_);
    curr_data_bytes_ = 0;
    duration_ = 0;
    cv_can_push_.notify_all();
}

void PacketQueue::Close() {
    std::unique_lock lk{mtx_};
    if (closed_) {
        return;
    }
    closed_ = true;
    cv_can_pop_.notify_all();
    cv_can_push_.notify_all();
}

std::size_t PacketQueue::GetTotalDataSize() const {
    std::lock_guard lk{mtx_};
    return curr_data_bytes_;
}

// =============================================================================
// FrameQueue 实现
// =============================================================================

FrameQueue::FrameQueue(int max_size)
    : max_size_(std::min(16, max_size)),  // NOTE: 与16比较取小的那个
      decoded_frames_(max_size_) {
    for (auto& decoded_frame : decoded_frames_) {
        decoded_frame.frame_.reset(av_frame_alloc());
        if (!decoded_frame.frame_) {
            throw std::runtime_error("分配 AVFrame 失败!");
        }
    }
}

DecodedFrame* FrameQueue::PeekWritable() {
    std::unique_lock lk{mtx_};
    cv_can_write_.wait(lk, [this] { return closed_ || size_ < max_size_; });
    if (closed_) {
        return nullptr;
    }
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

DecodedFrame* FrameQueue::PeekReadable() {
    std::unique_lock lk{mtx_};
    cv_can_read_.wait(lk, [this] { return closed_ || size_ > 0; });
    if (closed_ && size_ == 0) {
        return nullptr;
    }
    return &decoded_frames_[rindex_];
}

// 偏移读索引 rindex
void FrameQueue::MoveReadIndex() {
    std::unique_lock lk{mtx_};
    av_frame_unref(decoded_frames_[rindex_].frame_.get());  // NOTE: 这里会减少引用计数
    if (++rindex_ == max_size_) {
        rindex_ = 0;
    }
    --size_;
    cv_can_write_.notify_one();
}

std::size_t FrameQueue::GetSize() const {
    std::lock_guard lk{mtx_};
    return size_;
}

void FrameQueue::Close() {
    std::unique_lock lk{mtx_};
    if (closed_) {
        return;
    }
    closed_ = true;
    // 唤醒可能在等待写入的线程 (生产者)
    cv_can_write_.notify_all();
    // 唤醒可能在等待读取的线程 (消费者)
    cv_can_read_.notify_all();
}

void FrameQueue::Clear() {
    std::unique_lock lk{mtx_};
    // NOTE: 这里不需要清空环形队列, 因为是循环使用的, 只需要释放对应 AVFrame 的内存即可
    for (auto& decoded_frame : decoded_frames_) {
        av_frame_unref(decoded_frame.frame_.get());
    }
    size_ = 0;
    windex_ = 0;
    rindex_ = 0;
    cv_can_write_.notify_all();
}

}  // namespace avplayer