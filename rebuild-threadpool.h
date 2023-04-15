#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <thread>
#include <mutex>
#include <chrono>
#include <time.h>
#include <queue>
#include <future>
#include <functional>
#include <utility>
#include <vector>
#include <condition_variable>
#include <string>
#include <shared_mutex>
using namespace std;

mutex _m;


// * 线程安全队列
template<typename T>
class SafeQueue {
public:
    void push(const T &item) {
        {
            std::scoped_lock lock(mtx_);
            queue_.push(item);
        }
        cond_.notify_one();
    }
    void push(T &&item) {// 两个push方法，此处不是万能引用而是单纯右值
        {
            std::scoped_lock lock(mtx_);
            queue_.push(std::move(item));
        }
        cond_.notify_one();
    }
    // //将两个push合成一个
    // template <typename U>
    // void push(U&& item) {
    //     {
    //         static_assert(std::is_same<U,T>::value==true);
    //         std::scoped_lock lock(mtx_);
    //         queue_.push(std::forward(item));
    //     }
    //     cond_.notify_one();
    // }

    bool pop(T &item) {
        std::unique_lock lock(mtx_);
        cond_.wait(lock, [&]() {
            return !queue_.empty() || stop_;
        });
        if (queue_.empty())
            return false;
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    //加上const，不允许修改成员变量;可以被const对象调用
    std::size_t size() const {
        std::scoped_lock lock(mtx_);
        return queue_.size();
    }

    bool empty() const {
        std::scoped_lock lock(mtx_);
        return queue_.empty();
    }

    void stop() {
        {
            std::scoped_lock lock(mtx_);
            stop_ = true;
        }
        cond_.notify_all();
    }

private:
    std::condition_variable cond_;
    mutable std::mutex mtx_;    //mutable关键字让const成员函数也可以修改它
    std::queue<T> queue_;
    bool stop_ = false;
};

using WorkItem = std::function<void()>;
// * 简易多线程单任务队列线程池，使用SafeQueue线程安全队列。
class SimplePool {
public:
    explicit SimplePool(size_t threads = std::thread::hardware_concurrency()) {
        for (size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this]() {
                for (;;) {
                    std::function<void()> task;
                    if (!queue_.pop(task))
                        return;

                    if (task)
                        task();
                }
            });
        }
    }

    void enqueue(WorkItem item) {
        queue_.push(std::move(item));
    }

    ~SimplePool() {
        
        queue_.stop();
        for (auto& thd: workers_)
            thd.join();
    }

private:
    SafeQueue<WorkItem> queue_;
    std::vector<std::thread> workers_;
};

#endif