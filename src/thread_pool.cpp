#include "thread_pool.h"

ThreadPool::ThreadPool(size_t threads) : stop(false), maxThreads(threads) {
    resize(threads);
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers)
        worker.join();
}

void ThreadPool::resize(size_t newSize) {
    if (newSize > maxThreads) {
        newSize = maxThreads;
    }

    // 增加新的线程
    while (workers.size() < newSize) {
        workers.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queueMutex);
                    this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                    if (this->stop && this->tasks.empty())
                        return;
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }
                task();
            }
        });
    }

    // 安全地减少线程
    while (workers.size() > newSize) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();  // 唤醒所有线程，让它们检查 stop 状态
        for (std::thread &worker : workers) {
            if (worker.joinable()) {
                worker.join();  // 等待线程完成
            }
        }
        workers.pop_back();
    }
}
