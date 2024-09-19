#include "thread_pool.h"
#include <type_traits>
#include <future>
#include <stdexcept>

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {

    using return_type = typename std::invoke_result<F, Args...>::type;

    // 创建打包任务，包含函数和参数
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    // 获取任务的 future，稍后可以获取任务的结果
    std::future<return_type> res = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queueMutex);

        // 如果线程池已经停止，不允许添加任务
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        // 将任务加入任务队列中
        tasks.emplace([task](){ (*task)(); });
    }

    // 通知一个等待的工作线程，有新任务可执行
    condition.notify_one();
    
    return res;
}
