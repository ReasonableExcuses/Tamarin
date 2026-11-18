// benchmarks/async_bench/cpp_coroutines.cpp
// C++20 Coroutine Benchmark: High-throughput task churn & recursive tree co_await

#include <iostream>
#include <coroutine>
#include <chrono>
#include <cstdint>
#include <windows.h>

template <typename T>
struct Task {
    struct promise_type {
        T value{};
        std::exception_ptr exception;

        Task get_return_object() {
            return Task(std::coroutine_handle<promise_type>::from_promise(*this));
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_value(T v) noexcept { value = v; }
        void unhandled_exception() { exception = std::current_exception(); }
    };

    std::coroutine_handle<promise_type> handle;

    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    Task(Task&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    ~Task() {
        if (handle) handle.destroy();
    }

    bool await_ready() const noexcept {
        return handle ? handle.done() : true;
    }
    void await_suspend(std::coroutine_handle<> awaiting) noexcept {
        // Already completed in eager mode
    }
    T await_resume() {
        return handle.promise().value;
    }
};

Task<int> task_work(int x) {
    co_return x * 3 + 1;
}

Task<int> tree_sum(int depth, int val) {
    if (depth <= 0) {
        co_return val;
    }
    Task<int> left = tree_sum(depth - 1, val * 2);
    Task<int> right = tree_sum(depth - 1, val * 2 + 1);
    int l = co_await left;
    int r = co_await right;
    co_return l + r;
}

int main() {
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    // 1. High-frequency async task churn (50,000 tasks)
    int churn_sum = 0;
    for (int i = 0; i < 50000; i++) {
        Task<int> t = task_work(i);
        churn_sum += t.handle.promise().value;
    }

    // 2. Recursive tree reduction depth 14 (16,383 nodes)
    Task<int> tree_task = tree_sum(14, 1);
    int tree_res = tree_task.handle.promise().value;

    QueryPerformanceCounter(&end);
    double elapsed = (double)(end.QuadPart - start.QuadPart) * 1000.0 / (double)freq.QuadPart;
    int64_t checksum = (int64_t)churn_sum + (int64_t)tree_res;

    std::cout << "TIME_MS: " << elapsed << "\nCHECKSUM: " << checksum << "\n";
    return 0;
}
