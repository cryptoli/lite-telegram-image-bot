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
    for(std::thread &worker: workers)
        worker.join();
}

void ThreadPool::resize(size_t newSize) {
    if (newSize > maxThreads) {
        newSize = maxThreads;
    }
    
    while (workers.size() < newSize) {
        workers.emplace_back([this] {
            for(;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queueMutex);
                    this->condition.wait(lock, [this]{ return this->stop || !this->tasks.empty(); });
                    if(this->stop && this->tasks.empty())
                        return;
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }
                task();
            }
        });
    }
    
    while (workers.size() > newSize) {
        workers.back().detach();
        workers.pop_back();
    }
}