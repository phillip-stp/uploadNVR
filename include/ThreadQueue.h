#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

template <typename T>
class ThreadQueue {
    std::queue<T> queue;
    std::mutex mtx;
    std::condition_variable cv;

public:
    ThreadQueue() = default;
    ~ThreadQueue() = default;
    ThreadQueue(const ThreadQueue&) = delete;
    ThreadQueue& operator=(const ThreadQueue&) = delete;

    void push(T item) {
        std::lock_guard lock(mtx);
        queue.push(std::move(item));
        cv.notify_one();
    }

    void wait_and_pop(T& popped_item) {
        std::unique_lock lock(mtx);

        cv.wait(lock, [this]() { return !queue.empty(); });

        popped_item = std::move(queue.front());
        queue.pop();
    }

    bool empty() {
        std::lock_guard lock(mtx);
        return queue.empty();
    }
};