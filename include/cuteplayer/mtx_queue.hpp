// 基于互斥锁的线程安全队列

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>

// 线程安全的有锁队列模板类
template <typename T, typename Queue = std::queue<T>>
class MtxQueue {
private:
    Queue queue_;
    std::mutex mtx_;
    std::condition_variable cv_notfull_;   // 队列未满条件变量
    std::condition_variable cv_notempty_;  // 队列非空条件变量
    std::size_t limit_;                    // 最大允许堆积的元素数量

public:
    // -1转无符号最大数
    // 指定最大允许堆积的元素数量，超过该数量后会阻塞
    explicit MtxQueue(std::size_t limit = static_cast<std::size_t>(-1)) : limit_(limit) {}

public:
    // 向队列中推入元素
    void Push(T value) {
        std::unique_lock lk{mtx_};
        cv_notfull_.wait(lk, [this] { return queue_.size() < limit_; });
        queue_.push(std::move(value));
        cv_notempty_.notify_one();  // 通知可取
    }

    // 尝试向队列中推入元素，不阻塞
    bool TryPush(T value) {
        std::unique_lock lk{mtx_};
        if (queue_.size() >= limit_) {
            return false;
        }
        queue_.push(std::move(value));
        cv_notempty_.notify_one();
        return true;
    }

    // 从队列中取出元素(阻塞版本)
    T Pop() {
        std::unique_lock lk{mtx_};
        cv_notempty_.wait(lk, [this] { return !queue_.empty(); });  // 等待可取
        T value{std::move(queue_.front())};
        queue_.pop();
        cv_notfull_.notify_one();
        return value;
    }

    // 尝试从队列中取出元素(不阻塞版本)
    std::optional<T> TryPop() {
        std::unique_lock lk{mtx_};
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value{std::move(queue_.front())};
        queue_.pop();
        cv_notfull_.notify_one();
        return value;
    }

    // 尝试从队列中取出元素，等待一段时间，若超时则返回 nullopt
    std::optional<T> TryPopFor(std::chrono::steady_clock::duration timeout) {
        std::unique_lock lk{mtx_};
        if (!cv_notempty_.wait_for(lk, timeout, [this] { return !queue_.empty(); })) {
            return std::nullopt;
        }
        T value{std::move(queue_.front())};
        queue_.pop();
        cv_notfull_.notify_one();
        return value;
    }

    // 尝试从队列中取出元素，等待至一个时间点，若超时则返回 nullopt
    std::optional<T> TryPopUntil(std::chrono::steady_clock::time_point timeout) {
        std::unique_lock lk{mtx_};
        if (!cv_notempty_.wait_until(lk, timeout, [this] { return !queue_.empty(); })) {
            return std::nullopt;
        }
        T value{std::move(queue_.front())};
        queue_.pop();
        cv_notfull_.notify_one();
        return value;
    }

    bool Empty() const {
        std::unique_lock lk{mtx_};
        return queue_.empty();
    }
};
