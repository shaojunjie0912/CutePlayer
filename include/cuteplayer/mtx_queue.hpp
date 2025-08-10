#pragma once

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>

// 线程安全的有锁队列模板类
// NOTE: 在并发的生产者-消费者模型中，closed_ 标志位是实现“优雅停机”的标准且核心的实践
// NOTE: 如果 close 则会处理完队列中的元素 (这是 close‑drain)
// 其他还有 close-immediate: 直接丢弃
template <typename T, typename Queue = std::queue<T>>
class MtxQueue {
private:
    Queue queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_can_push_;  // 队列未满条件变量
    std::condition_variable cv_can_pop_;   // 队列非空条件变量
    std::size_t limit_;                    // 最大允许堆积的元素数量
    bool closed_{false};                   // 队列是否已关闭

public:
    // -1转无符号最大数
    // 指定最大允许堆积的元素数量，超过该数量后会阻塞
    explicit MtxQueue(std::size_t limit = static_cast<std::size_t>(-1)) : limit_(limit) {
        assert(limit_ > 0 && "limit must be > 0");  // limit > 0
    }
    MtxQueue(const MtxQueue&) = delete;  // 禁止拷贝
    MtxQueue(MtxQueue&&) = delete;       // 禁止移动

public:
    void Close() {
        std::unique_lock lk{mtx_};
        closed_ = true;
        cv_can_push_.notify_all();
        cv_can_pop_.notify_all();
    }

public:
    // 阻塞 Push (左值)
    bool Push(const T& value) { return Emplace(value); }

    // 阻塞 Push (右值)
    bool Push(T&& value) { return Emplace(std::move(value)); }

    // 非阻塞 Push (左值)
    bool TryPush(const T& value) { return TryEmplace(value); }

    // 非阻塞 Push (右值)
    bool TryPush(T&& value) { return TryEmplace(std::move(value)); }

    // 限时 Push (时间段) 左值
    template <class Rep, class Period>
    bool TryPushFor(const std::chrono::duration<Rep, Period>& d, const T& v) {
        return TryEmplaceFor(d, v);
    }

    // 限时 Push (时间段) 右值
    template <class Rep, class Period>
    bool TryPushFor(const std::chrono::duration<Rep, Period>& d, T&& v) {
        return TryEmplaceFor(d, std::move(v));
    }

    // 限时 Push (时间点) 左值
    template <class Clock, class Duration>
    bool TryPushUntil(const std::chrono::time_point<Clock, Duration>& tp, const T& v) {
        return TryEmplaceUntil(tp, v);
    }

    // 限时 Push (时间点) 右值
    template <class Clock, class Duration>
    bool TryPushUntil(const std::chrono::time_point<Clock, Duration>& tp, T&& v) {
        return TryEmplaceUntil(tp, std::move(v));
    }

    // 阻塞 Emplace
    template <typename... Args>
    bool Emplace(Args&&... args) {
        std::unique_lock lk{mtx_};
        cv_can_push_.wait(lk, [this] { return closed_ || queue_.size() < limit_; });
        if (closed_) {
            return false;
        }
        // NOTE: 完美转发 + 原地构造, 避免拷贝/移动
        queue_.emplace(std::forward<Args>(args)...);
        cv_can_pop_.notify_one();  // 通知可取
        return true;
    }

    // 非阻塞 Emplace
    template <typename... Args>
    bool TryEmplace(Args&&... args) {
        std::unique_lock lk{mtx_};
        if (closed_ || queue_.size() >= limit_) {
            return false;
        }
        queue_.emplace(std::forward<Args>(args)...);
        cv_can_pop_.notify_one();
        return true;
    }

    // 限时 Emplace (时间段)
    template <class Rep, class Period, class... Args>
    bool TryEmplaceFor(const std::chrono::duration<Rep, Period>& duration, Args&&... args) {
        std::unique_lock lk{mtx_};
        if (!cv_can_push_.wait_for(lk, duration,
                                   [this] { return closed_ || queue_.size() < limit_; })) {
            return false;
        }
        if (closed_) {
            return false;
        }
        queue_.emplace(std::forward<Args>(args)...);
        cv_can_pop_.notify_one();
        return true;
    }

    // 限时 Emplace (时间点)
    template <class Clock, class Duration, class... Args>
    bool TryEmplaceUntil(const std::chrono::time_point<Clock, Duration>& time_point,
                         Args&&... args) {
        std::unique_lock lk{mtx_};
        if (!cv_can_push_.wait_until(lk, time_point,
                                     [this] { return closed_ || queue_.size() < limit_; })) {
            return false;
        }
        if (closed_) {
            return false;
        }
        queue_.emplace(std::forward<Args>(args)...);
        cv_can_pop_.notify_one();
        return true;
    }

    // 阻塞 Pop
    std::optional<T> Pop() {
        std::unique_lock lk{mtx_};
        cv_can_pop_.wait(lk, [this] { return closed_ || !queue_.empty(); });  // 等待可取
        if (queue_.empty()) {                                                 // 为空且已关闭
            return std::nullopt;
        }
        T value{std::move(queue_.front())};
        queue_.pop();
        cv_can_push_.notify_one();
        return value;
    }

    // 非阻塞 Pop
    std::optional<T> TryPop() {
        std::unique_lock lk{mtx_};
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value{std::move(queue_.front())};
        queue_.pop();
        cv_can_push_.notify_one();
        return value;
    }

    // 限时 Pop (时间段)
    template <typename Rep, typename Period>
    std::optional<T> TryPopFor(std::chrono::duration<Rep, Period> const& duration) {
        std::unique_lock lk{mtx_};
        if (!cv_can_pop_.wait_for(lk, duration, [this] { return closed_ || !queue_.empty(); })) {
            return std::nullopt;
        }
        if (queue_.empty()) {  //  NOTE: 为空且已关闭
            return std::nullopt;
        }
        T value{std::move(queue_.front())};
        queue_.pop();
        cv_can_push_.notify_one();
        return value;
    }

    // 限时 Pop (时间点)
    template <typename Clock, typename Duration>
    std::optional<T> TryPopUntil(std::chrono::time_point<Clock, Duration> const& time_point) {
        std::unique_lock lk{mtx_};
        if (!cv_can_pop_.wait_until(lk, time_point,
                                    [this] { return closed_ || !queue_.empty(); })) {
            return std::nullopt;
        }
        if (queue_.empty()) {  //  NOTE: 为空且已关闭
            return std::nullopt;
        }
        T value{std::move(queue_.front())};
        queue_.pop();
        cv_can_push_.notify_one();
        return value;
    }

    bool Empty() const {
        std::lock_guard lk{mtx_};
        return queue_.empty();
    }

    std::size_t Size() const {
        std::lock_guard lk{mtx_};
        return queue_.size();
    }

    void Clear() {
        std::lock_guard lk{mtx_};
        Queue{}.swap(queue_);  // copy-swap
        cv_can_push_.notify_all();
    }
};