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
//需要-std=c++17

//shared_lock 是一种共享的、非独占的锁，可以允许多个线程同时持有该锁，并且不会阻塞其他线程的读取操作，从而提高程序的并发性能。
//支持同时读，不支持同时写入
template<class T>
struct safe_queue {
    queue<T> que;
    shared_mutex _m;
    bool empty(){
        shared_lock<shared_mutex> lock(_m);
        return que.empty();
    }
    size_t size(){
        shared_lock<shared_mutex> lock(_m);
        return que.size();
    }
    void push(T& t){
        shared_lock<shared_mutex> lock(_m);
        que.push(t);
    }
    bool pop(T& t){
        shared_lock<shared_mutex> lock(_m);
        if (que.empty()) return false;
        t=move(que.front());
        que.pop();
        return true;
    }
};

class Threadpool
{
    /* data */
private:
    class worker {
    public:
        Threadpool* pool;
        //每个 worker 线程会不断地从任务队列中取出任务并执行，直到线程池被关闭。
        //void operator()()函数：是 worker 线程的实际执行函数，也是一个函数对象
        //unique_lock 更加灵活，可以在需要时手动 lock 和 unlock，而 lock_guard 只能在构造函数中 lock，析构函数中 unlock，不能手动控制锁的生命周期。
        worker(Threadpool* _p):pool(_p){}
        void operator ()() {
            while(!pool->is_shut_down) {
                {
                    unique_lock<mutex> lock(pool->_m);
                    //在 lock 的作用域内进行等待 cv 的通知，直到线程池被关闭或者任务队列非空，才会从等待中被唤醒。
                    pool->cv.wait(lock,[this](){
                    return this->pool->is_shut_down || !this->pool->que.empty(); 
                    });
                }//unique_lock<mutex> 对象被释放，其它线程可以继续竞争 pool->_m 的访问权
                function<void()> func;
                bool flag = pool->que.pop(func);
                if (flag) {
                    func();
                }
            }
        }
    };
public:
    bool is_shut_down;
    //std::function<void()> 是一个可调用对象的封装器，它可以包含任何可调用对象（例如函数指针、成员函数指针、仿函数或 lambda 表达式）。<void()> 是该可调用对象的函数签名，表示它没有参数并且没有返回值。
    //可以使用 lambda 表达式创建一个这样的函数对象：auto func = [](){ //pss }; 执行时直接func();
    safe_queue<std::function<void()>> que;
    vector<std::thread> threads;
    mutex _m;
    condition_variable cv;
    //语法上threads可以是个int，被初始化为n；也可以是vector，被初始化为n个元素的数组。
    //花括号初始化方式。避免类型收窄。编译器没提醒,但g++ file.cpp -std=c++11时会报错说“cannot be narrowed”
    Threadpool(int n):threads(n), is_shut_down{false} {
        //用同一个 worker 对象初始化所有的线程。避免为每个线程创建一个独立的 worker 对象
        for(auto& t : threads) t = thread{worker(this)};
    }
    Threadpool(const Threadpool&)=delete;
    Threadpool(Threadpool&&)=delete;
    Threadpool& operator = (const Threadpool&)=delete;
    Threadpool& operator = (Threadpool&&)=delete;

    //submit函数，用于向线程池中提交任务。其中，submit函数的返回值是一个future对象，表示异步执行任务的结果。
    template<typename F, typename... Args>
    auto submit(F&& f ,Args&&...args) -> std::future<decltype(f(args...))> {
    //c++11，单独auto不能用于函数形参的类型推导。c++14前也不能直接推导函数返回值，需结合decltype
        //function<decltype(f(args...))()>的意思是一个不带参数的函数对象（即可调用对象），其返回类型为decltype(f(args...))
        //直接auto也可以
        //function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);也可以
        function<decltype(f(args...))()> func = [&f, args...](){return f(args...);};

        //创建一个指向std::packaged_task对象的shared_ptr智能指针，（为了可以拷贝构造）。
        //std::packaged_task是一个包装器，可以将任何可调用对象（比如函数、函数对象或者Lambda表达式）封装成一个异步操作，返回一个std::future对象，用于获取异步操作的结果。
        //需要用一个shared_ptr智能指针来对packaged_task进行包装，这是因为在std::function<void()>中会尝试生成std::packaged_task的拷贝构造函数，而std::packaged_task是禁止进行拷贝操作的，这会引起编译器的报错
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);
        std::function<void()> warpper_func = [task_ptr](){
            (*task_ptr)();
        };
        que.push(warpper_func);
        cv.notify_one();    //唤醒一个线程
        return task_ptr->get_future();
    }
    //get_future用于获取尚未完成的异步任务的std::future对象，只读；而get用于阻塞当前线程，等待异步任务完成并获得结果。

    //这个线程池的析构函数中，首先会提交一个空函数到任务队列中，确保所有任务都被处理完毕；然后将 is_shut_down 标志位置为 true，通知所有线程池中的任务执行完成后可以退出了；最后等待所有线程结束。
    ~Threadpool(){
        //为什么不加这两行会出错？
        //因为在没有调用f.get()的情况下，当线程池被销毁时，仍有未完成的任务在队列中，但是没有线程能够继续执行这些任务，从而导致程序出现错误。
        auto f = submit([](){});
        f.get();
        is_shut_down=true;
        cv.notify_all();
        //依次等待每个线程执行结束，但这些线程之间是并发执行的。
        for (auto& t : threads){
            if(t.joinable()) t.join();
        }
    }
};

#endif


