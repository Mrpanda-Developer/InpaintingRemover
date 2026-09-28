#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace subtitle_remover {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) throw std::invalid_argument("queue capacity must be positive");
    }

    bool push(T value) {
        std::unique_lock lock(mutex_);
        writable_.wait(lock, [&] { return closed_ || queue_.size() < capacity_; });
        if (closed_) return false;
        queue_.push(std::move(value));
        readable_.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        readable_.wait(lock, [&] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return std::nullopt;
        T value = std::move(queue_.front());
        queue_.pop();
        writable_.notify_one();
        return std::optional<T>(std::move(value));
    }

    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        readable_.notify_all();
        writable_.notify_all();
    }

private:
    const std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable readable_;
    std::condition_variable writable_;
    std::queue<T> queue_;
    bool closed_ = false;
};

} // namespace subtitle_remover