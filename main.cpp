#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <memory>

using namespace std;

class TThreadPool {
public:
    TThreadPool(size_t threads);
    ~TThreadPool();

    template<typename F, typename... Args>
    future<typename result_of<F(Args...)>::type> AddTask(F&& f, Args&&... args);
private:
    mutex queue_lock;
    vector<thread> threads;
    void Clear();
    void Worker();
    bool Done;
    condition_variable cv;
    queue<function<void()>> tasks;
};

TThreadPool::TThreadPool(size_t threadsCount) : Done(false) {
    try {
        for (size_t i = 0; i < threadsCount; ++i) {
            threads.emplace_back(&TThreadPool::Worker, this);
        }
    } catch(...) {
        Clear();
    }
}

TThreadPool::~TThreadPool() {
    Clear();
}

void TThreadPool::Clear() {
    {
        lock_guard<mutex> g(queue_lock);
        Done = true;
    }
    cv.notify_all();
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void TThreadPool::Worker() {
    while (true) {
        function<void()> task;
        {
            unique_lock<mutex> lock(queue_lock);
            cv.wait(lock, [this] {return this->Done || !this->tasks.empty();});
            if (Done && tasks.empty()) {
                return;
            }
            task = move(tasks.front());
            tasks.pop();
        }
        task();
    }
}

template<typename F, typename... Args>
future<typename result_of<F(Args...)>::type> TThreadPool::AddTask(F&& f, Args&&... args) {
    using result_type = typename result_of<F(Args...)>::type;
    auto packed_task = make_shared<packaged_task<result_type ()>>(bind(forward<F>(f), forward<Args>(args)...));
    auto res = packed_task->get_future();
    {
        lock_guard<mutex> g(queue_lock);
        tasks.emplace([packed_task] (){(*packed_task)();});
    }
    cv.notify_one();
    return res;
}

int foo(int& x) {
    x = 100;
    return x * 20;
}

int main() {
    TThreadPool a(2);
    int b = 10;
    auto res = a.AddTask(foo, ref(b));
    cout << res.get() << " " << b << endl;
    return 0;
}
