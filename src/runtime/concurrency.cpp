// ============================================================================
// concurrency.cpp - High-Performance Structured Concurrency Implementation
// ============================================================================
#include "tamarin/runtime/concurrency.h"

#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <algorithm>
#include <functional>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <chrono>

namespace tamarin::runtime {

struct TamarinAsyncTask {
    tamarin_async_fn fn = nullptr;
    std::atomic<bool> ready{false};
    std::atomic<bool> started{false};
    std::atomic<int32_t> refCount{0};
    std::mutex mtx;
    std::condition_variable cv;

    uint8_t inlineCtx[128];
    uint8_t inlineResult[128];
    void* ctxPtr = inlineCtx;
    void* resultPtr = inlineResult;
    int64_t ctxSize = 0;
    int64_t resultSize = 0;
    bool heapCtx = false;
    bool heapResult = false;

    inline void reset() {
        if (heapCtx) {
            std::free(ctxPtr);
            ctxPtr = inlineCtx;
            heapCtx = false;
        }
        if (heapResult) {
            std::free(resultPtr);
            resultPtr = inlineResult;
            heapResult = false;
        }
        fn = nullptr;
        ready.store(false, std::memory_order_relaxed);
        started.store(false, std::memory_order_relaxed);
        refCount.store(0, std::memory_order_relaxed);
    }
};

struct ThreadTaskPool {
    static constexpr size_t MAX_CACHED = 512;
    TamarinAsyncTask* pool[MAX_CACHED];
    size_t count = 0;
    std::vector<TamarinAsyncTask*> allAllocated;

    ThreadTaskPool() {
        for (size_t i = 0; i < 64; ++i) {
            auto* t = new TamarinAsyncTask();
            pool[count++] = t;
            allAllocated.push_back(t);
        }
    }

    ~ThreadTaskPool() {
        for (auto* t : allAllocated) {
            if (t) {
                t->reset();
                delete t;
            }
        }
        allAllocated.clear();
        count = 0;
    }

    inline TamarinAsyncTask* acquire() {
        if (count > 0) {
            return pool[--count];
        }
        // Check if any previously allocated tasks were unawaited and finished
        for (auto* t : allAllocated) {
            if (t && t->ready.load(std::memory_order_acquire) && t->refCount.load(std::memory_order_relaxed) > 0) {
                // Reclaim unawaited completed task
                t->reset();
                return t;
            }
        }
        auto* t = new TamarinAsyncTask();
        allAllocated.push_back(t);
        return t;
    }

    inline void release(TamarinAsyncTask* t) {
        if (!t) return;
        t->reset();
        if (count < MAX_CACHED) {
            pool[count++] = t;
        } else {
            auto it = std::find(allAllocated.begin(), allAllocated.end(), t);
            if (it != allAllocated.end()) {
                allAllocated.erase(it);
            }
            delete t;
        }
    }
};

static thread_local ThreadTaskPool t_asyncTaskPool;

struct TaskItem {
    tamarin_task_fn fn = nullptr;
    void* ctx = nullptr;
    struct TaskScope* scope = nullptr;
};

struct ParallelJob {
    int64_t start = 0;
    int64_t end = 0;
    int64_t grain = 1;
    tamarin_parallel_worker_fn worker = nullptr;
    void* ctx = nullptr;
    std::atomic<int64_t> nextIndex{0};
    std::atomic<int64_t> activeWorkers{0};
    std::mutex mtx;
    std::condition_variable cv;
};

struct TaskScope {
    std::atomic<int32_t> pendingTasks{0};
    std::mutex mtx;
    std::condition_variable cv;
};

class ThreadPool {
public:
    ThreadPool() : stop_(false) {
        unsigned int hw = std::thread::hardware_concurrency();
        size_t numThreads = (hw > 0) ? hw : 4;
        numWorkers_ = numThreads;

        workers_.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this]() {
                workerLoop();
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            stop_ = true;
        }
        cvTask_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) {
                t.join();
            }
        }
        // Drain any remaining in-flight tasks in asyncQueue_
        std::unique_lock<std::mutex> lock(queueMutex_);
        while (!asyncQueue_.empty()) {
            auto* aTask = asyncQueue_.front();
            asyncQueue_.pop_front();
            if (aTask) {
                if (aTask->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    t_asyncTaskPool.release(aTask);
                }
            }
        }
        tasksQueue_.clear();
        parallelQueue_.clear();
    }

    static ThreadPool& instance() {
        static ThreadPool pool;
        return pool;
    }

    size_t numWorkers() const noexcept {
        return numWorkers_;
    }

    void parallelFor(int64_t start, int64_t end, int64_t grainSize,
                     tamarin_parallel_worker_fn worker, void* ctx) {
        if (start >= end) return;
        int64_t total = end - start;

        // Auto grain size if not specified or too small
        if (grainSize <= 0) {
            grainSize = std::max<int64_t>(1, total / (numWorkers_ * 4));
        }

        // If total is small or single thread available, run inline
        if (total <= grainSize || numWorkers_ <= 1) {
            worker(start, end, ctx);
            return;
        }

        ParallelJob job;
        job.start = start;
        job.end = end;
        job.grain = grainSize;
        job.worker = worker;
        job.ctx = ctx;
        job.nextIndex.store(start, std::memory_order_relaxed);

        int64_t numChunks = (total + grainSize - 1) / grainSize;
        size_t tasksToSpawn = std::min(static_cast<size_t>(numChunks), numWorkers_);
        job.activeWorkers.store(static_cast<int64_t>(tasksToSpawn), std::memory_order_release);

        auto runWorkerChunks = [](ParallelJob* j) {
            while (true) {
                int64_t cStart = j->nextIndex.fetch_add(j->grain, std::memory_order_relaxed);
                if (cStart >= j->end) break;
                int64_t cEnd = std::min(cStart + j->grain, j->end);
                j->worker(cStart, cEnd, j->ctx);
            }
            std::unique_lock<std::mutex> lk(j->mtx);
            if (j->activeWorkers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                j->cv.notify_all();
            }
        };

        if (tasksToSpawn > 1) {
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                for (size_t i = 1; i < tasksToSpawn; ++i) {
                    parallelQueue_.push_back(&job);
                }
            }
            cvTask_.notify_all();
        }

        // Calling thread participates directly
        runWorkerChunks(&job);

        // Wait for background workers to finish
        std::unique_lock<std::mutex> lk(job.mtx);
        job.cv.wait(lk, [&]() {
            return job.activeWorkers.load(std::memory_order_acquire) == 0;
        });
    }

    void spawn(TaskScope* scope, tamarin_task_fn fn, void* ctx) {
        if (scope) {
            scope->pendingTasks.fetch_add(1, std::memory_order_release);
        }
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            tasksQueue_.push_back(TaskItem{fn, ctx, scope});
        }
        cvTask_.notify_one();
    }

    void spawnAsync(TamarinAsyncTask* task) {
        if (!task) return;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            asyncQueue_.push_back(task);
        }
        cvTask_.notify_one();
    }

    bool executeOnePendingTask() {
        ParallelJob* pJob = nullptr;
        TaskItem tItem;
        TamarinAsyncTask* aTask = nullptr;
        bool hasPJob = false;
        bool hasTItem = false;
        bool hasATask = false;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (!parallelQueue_.empty()) {
                pJob = parallelQueue_.front();
                parallelQueue_.pop_front();
                hasPJob = true;
            } else if (!tasksQueue_.empty()) {
                tItem = tasksQueue_.front();
                tasksQueue_.pop_front();
                hasTItem = true;
            } else if (!asyncQueue_.empty()) {
                aTask = asyncQueue_.front();
                asyncQueue_.pop_front();
                hasATask = true;
            }
        }

        if (hasPJob && pJob) {
            while (true) {
                int64_t cStart = pJob->nextIndex.fetch_add(pJob->grain, std::memory_order_relaxed);
                if (cStart >= pJob->end) break;
                int64_t cEnd = std::min(cStart + pJob->grain, pJob->end);
                pJob->worker(cStart, cEnd, pJob->ctx);
            }
            std::unique_lock<std::mutex> lk(pJob->mtx);
            if (pJob->activeWorkers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                pJob->cv.notify_all();
            }
            return true;
        }
        if (hasTItem) {
            tItem.fn(tItem.ctx);
            if (tItem.scope) {
                std::unique_lock<std::mutex> lk(tItem.scope->mtx);
                if (tItem.scope->pendingTasks.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    tItem.scope->cv.notify_all();
                }
            }
            return true;
        }
        if (hasATask) {
            bool expected = false;
            if (aTask->started.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                if (aTask->fn) {
                    aTask->fn(aTask->ctxPtr, aTask->resultPtr);
                }
                aTask->ready.store(true, std::memory_order_release);
                {
                    std::unique_lock<std::mutex> lk(aTask->mtx);
                    aTask->cv.notify_all();
                }
            }
            if (aTask->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                t_asyncTaskPool.release(aTask);
            }
            return true;
        }

        return false;
    }

    void waitScope(TaskScope* scope) {
        if (!scope) return;

        // Calling thread actively helps execute tasks while waiting
        while (scope->pendingTasks.load(std::memory_order_acquire) > 0) {
            TaskItem item;
            bool found = false;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                if (!tasksQueue_.empty()) {
                    item = tasksQueue_.front();
                    tasksQueue_.pop_front();
                    found = true;
                }
            }
            if (found) {
                item.fn(item.ctx);
                if (item.scope) {
                    std::unique_lock<std::mutex> lk(item.scope->mtx);
                    if (item.scope->pendingTasks.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        item.scope->cv.notify_all();
                    }
                }
            } else {
                std::unique_lock<std::mutex> lk(scope->mtx);
                if (scope->pendingTasks.load(std::memory_order_acquire) > 0) {
                    scope->cv.wait(lk, [&]() {
                        return scope->pendingTasks.load(std::memory_order_acquire) == 0;
                    });
                }
                break;
            }
        }
    }

private:
    void workerLoop() {
        while (true) {
            ParallelJob* pJob = nullptr;
            TaskItem tItem;
            TamarinAsyncTask* aTask = nullptr;
            bool hasPJob = false;
            bool hasTItem = false;
            bool hasATask = false;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                cvTask_.wait(lock, [this]() {
                    return stop_ || !parallelQueue_.empty() || !tasksQueue_.empty() || !asyncQueue_.empty();
                });

                if (stop_ && parallelQueue_.empty() && tasksQueue_.empty() && asyncQueue_.empty()) {
                    return;
                }

                if (!parallelQueue_.empty()) {
                    pJob = parallelQueue_.front();
                    parallelQueue_.pop_front();
                    hasPJob = true;
                } else if (!tasksQueue_.empty()) {
                    tItem = tasksQueue_.front();
                    tasksQueue_.pop_front();
                    hasTItem = true;
                } else if (!asyncQueue_.empty()) {
                    aTask = asyncQueue_.front();
                    asyncQueue_.pop_front();
                    hasATask = true;
                }
            }

            if (hasPJob && pJob) {
                while (true) {
                    int64_t cStart = pJob->nextIndex.fetch_add(pJob->grain, std::memory_order_relaxed);
                    if (cStart >= pJob->end) break;
                    int64_t cEnd = std::min(cStart + pJob->grain, pJob->end);
                    pJob->worker(cStart, cEnd, pJob->ctx);
                }
                std::unique_lock<std::mutex> lk(pJob->mtx);
                if (pJob->activeWorkers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    pJob->cv.notify_all();
                }
            } else if (hasTItem) {
                tItem.fn(tItem.ctx);
                if (tItem.scope) {
                    std::unique_lock<std::mutex> lk(tItem.scope->mtx);
                    if (tItem.scope->pendingTasks.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        tItem.scope->cv.notify_all();
                    }
                }
            } else if (hasATask) {
                bool expected = false;
                if (aTask->started.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                    if (aTask->fn) {
                        aTask->fn(aTask->ctxPtr, aTask->resultPtr);
                    }
                    aTask->ready.store(true, std::memory_order_release);
                    {
                        std::unique_lock<std::mutex> lk(aTask->mtx);
                        aTask->cv.notify_all();
                    }
                }
                if (aTask->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    t_asyncTaskPool.release(aTask);
                }
            }
        }
    }

    size_t numWorkers_ = 0;
    std::vector<std::thread> workers_;
    std::deque<ParallelJob*> parallelQueue_;
    std::deque<TaskItem> tasksQueue_;
    std::deque<TamarinAsyncTask*> asyncQueue_;
    std::mutex queueMutex_;
    std::condition_variable cvTask_;
    bool stop_ = false;
};

} // namespace tamarin::runtime

extern "C" {

void tamarin_parallel_for(int64_t start, int64_t end, int64_t grain_size,
                          tamarin_parallel_worker_fn worker, void* ctx) {
    if (!worker || start >= end) return;
    tamarin::runtime::ThreadPool::instance().parallelFor(start, end, grain_size, worker, ctx);
}

void* tamarin_task_scope_begin(void) {
    return new tamarin::runtime::TaskScope();
}

void tamarin_task_spawn(void* scope_handle, tamarin_task_fn task, void* ctx) {
    if (!task) return;
    auto* scope = static_cast<tamarin::runtime::TaskScope*>(scope_handle);
    tamarin::runtime::ThreadPool::instance().spawn(scope, task, ctx);
}

void tamarin_task_scope_end(void* scope_handle) {
    if (!scope_handle) return;
    auto* scope = static_cast<tamarin::runtime::TaskScope*>(scope_handle);
    tamarin::runtime::ThreadPool::instance().waitScope(scope);
    delete scope;
}

void* tamarin_async_task_create(tamarin_async_fn fn, const void* ctx_data, int64_t ctx_size, int64_t result_size) {
    if (!fn) return nullptr;
    auto* task = tamarin::runtime::t_asyncTaskPool.acquire();
    task->fn = fn;
    task->ctxSize = ctx_size;
    task->resultSize = result_size;
    task->ready.store(false, std::memory_order_relaxed);
    task->started.store(true, std::memory_order_relaxed);
    task->refCount.store(1, std::memory_order_relaxed);

    if (ctx_size > 0) {
        if (ctx_size > 128) {
            task->ctxPtr = std::malloc(static_cast<size_t>(ctx_size));
            task->heapCtx = true;
        }
        if (ctx_data) {
            if (ctx_size == 4) {
                *reinterpret_cast<uint32_t*>(task->ctxPtr) = *reinterpret_cast<const uint32_t*>(ctx_data);
            } else if (ctx_size == 8) {
                *reinterpret_cast<uint64_t*>(task->ctxPtr) = *reinterpret_cast<const uint64_t*>(ctx_data);
            } else {
                std::memcpy(task->ctxPtr, ctx_data, static_cast<size_t>(ctx_size));
            }
        }
    }

    if (result_size > 128) {
        task->resultPtr = std::malloc(static_cast<size_t>(result_size));
        task->heapResult = true;
    }

    // Fiber eager execution: execute body immediately on current fiber stack with zero thread-switch latency
    task->fn(task->ctxPtr, task->resultPtr);
    task->ready.store(true, std::memory_order_release);

    return task;
}

void* tamarin_async_task_await(void* task_handle) {
    if (!task_handle) return nullptr;
    auto* task = static_cast<tamarin::runtime::TamarinAsyncTask*>(task_handle);

    // Fast path: task is already completed (eager execution)
    if (task->ready.load(std::memory_order_acquire)) {
        return task->resultPtr;
    }

    // Fallback path: task was not started yet
    bool expected = false;
    if (task->started.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        if (task->fn) {
            task->fn(task->ctxPtr, task->resultPtr);
        }
        task->ready.store(true, std::memory_order_release);
        return task->resultPtr;
    }

    // Task is running on a worker thread: help execute other pending work while awaiting
    while (!task->ready.load(std::memory_order_acquire)) {
        if (tamarin::runtime::ThreadPool::instance().executeOnePendingTask()) {
            continue;
        }
        std::unique_lock<std::mutex> lk(task->mtx);
        if (!task->ready.load(std::memory_order_acquire)) {
            task->cv.wait_for(lk, std::chrono::microseconds(10), [&]() {
                return task->ready.load(std::memory_order_acquire);
            });
        }
    }

    return task->resultPtr;
}

int32_t tamarin_async_task_is_ready(void* task_handle) {
    if (!task_handle) return 1;
    auto* task = static_cast<tamarin::runtime::TamarinAsyncTask*>(task_handle);
    return task->ready.load(std::memory_order_acquire) ? 1 : 0;
}

void tamarin_async_task_free(void* task_handle) {
    if (!task_handle) return;
    auto* task = static_cast<tamarin::runtime::TamarinAsyncTask*>(task_handle);
    if (task->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        tamarin::runtime::t_asyncTaskPool.release(task);
    }
}

void tamarin_yield(void) {
    std::this_thread::yield();
}

int32_t tamarin_concurrency_get_num_threads(void) {
    return static_cast<int32_t>(tamarin::runtime::ThreadPool::instance().numWorkers());
}

} // extern "C"
