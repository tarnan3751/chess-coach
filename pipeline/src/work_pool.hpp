// run_pool: the pipeline's thread model. The calling thread produces items in order (it reads
// and cuts the stream) into a bounded queue of 2 * threads items; `threads` workers pop them
// and consume them, each with its own state, indexed by worker. Memory is bounded by the queue,
// never by the stream. An exception thrown by the producer or a worker stops the others, and
// the first one is rethrown on the calling thread once every thread has joined.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace chesscoach::pipeline {

template <typename Item> class BoundedQueue {
  public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    // Blocks while the queue is full; false once the queue is closed.
    bool push(Item item) {
        std::unique_lock lock(mutex_);
        not_full_.wait(lock, [this] { return closed_ || items_.size() < capacity_; });
        if (closed_) {
            return false;
        }
        items_.push_back(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    // Blocks while the queue is empty and open; nullopt once it is closed and drained, or
    // aborted.
    std::optional<Item> pop() {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [this] { return closed_ || !items_.empty(); });
        if (items_.empty() || aborted_) {
            return std::nullopt;
        }
        Item item = std::move(items_.front());
        items_.pop_front();
        not_full_.notify_one();
        return item;
    }

    // No more items: pop drains what is queued, then returns nullopt.
    void close() {
        const std::scoped_lock lock(mutex_);
        closed_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    // Stop now: push and pop return at once, queued items are dropped.
    void abort() {
        const std::scoped_lock lock(mutex_);
        closed_ = true;
        aborted_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

  private:
    std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::deque<Item> items_;
    std::size_t capacity_;
    bool closed_ = false;
    bool aborted_ = false;
};

// Runs produce(push) on the calling thread, where push(item) returns false once the run is
// stopping, and consume(worker, item) on `threads` workers, worker in [0, threads).
template <typename Item>
void run_pool(std::size_t threads,
              const std::function<void(const std::function<bool(Item)>&)>& produce,
              const std::function<void(std::size_t, Item&)>& consume) {
    BoundedQueue<Item> queue(2 * threads);
    std::mutex error_mutex;
    std::exception_ptr error;
    const auto record = [&](const std::exception_ptr& caught) {
        const std::scoped_lock lock(error_mutex);
        if (!error) {
            error = caught;
        }
        queue.abort();
    };

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (std::size_t worker = 0; worker < threads; ++worker) {
        workers.emplace_back([&, worker] {
            try {
                while (auto item = queue.pop()) {
                    consume(worker, *item);
                }
            } catch (...) {
                record(std::current_exception());
            }
        });
    }
    try {
        produce([&](Item item) { return queue.push(std::move(item)); });
        queue.close();
    } catch (...) {
        record(std::current_exception());
    }
    for (std::thread& thread : workers) {
        thread.join();
    }
    if (error) {
        std::rethrow_exception(error);
    }
}

} // namespace chesscoach::pipeline
