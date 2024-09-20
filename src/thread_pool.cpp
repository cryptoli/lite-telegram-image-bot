#include "thread_pool.h"

ThreadPool::ThreadPool(size_t threads) : stop(false), maxThreads(threads) {
    resize(threads);
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;  // 设置 stop 标志，表示停止所有线程
    }
    condition.notify_all();  // 唤醒所有线程
    for (std::thread &worker : workers) {
        if (worker.joinable()) {
            worker.join();  // 等待所有线程退出
        }
    }
}

void ThreadPool::resize(size_t newSize) {
    // 限制新大小不超过最大线程数
    if (newSize > maxThreads) {
        newSize = maxThreads;
    }

    // 增加线程
    while (workers.size() < newSize) {
        workers.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queueMutex);
                    this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                    
                    if (this->stop && this->tasks.empty()) {
                        return;  // 线程退出
                    }

                    // 获取任务并解锁队列
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }
                // 执行任务（锁外执行，防止阻塞队列）
                task();
            }
        });
    }

    // 减少线程
    while (workers.size() > newSize) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.push([this] {  // 向任务队列推送 "空任务"，让线程退出
                stop = true;
            });
        }
        condition.notify_all();  // 唤醒线程

        for (std::thread &worker : workers) {
            if (worker.joinable()) {
                worker.join();  // 等待线程结束
            }
        }
        workers.pop_back();  // 安全地减少线程
    }
}
