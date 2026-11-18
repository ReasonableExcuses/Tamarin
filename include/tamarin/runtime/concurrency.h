// ============================================================================
// concurrency.h - High-Performance Structured Concurrency & Parallel Execution
// ============================================================================
#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/// Worker function signature for parallel for loops:
/// Executes iterations in range [chunk_start, chunk_end) using captured context.
typedef void (*tamarin_parallel_worker_fn)(int64_t chunk_start, int64_t chunk_end, void* ctx);

/// Worker function signature for spawned tasks:
typedef void (*tamarin_task_fn)(void* ctx);

/// Worker function signature for asynchronous tasks:
/// Executes asynchronous task using context ctx and writes result into result_ptr.
typedef void (*tamarin_async_fn)(void* ctx, void* result_ptr);

/// Executes a canonical loop range [start, end) across worker threads.
/// If grain_size <= 0, the runtime automatically partitions the range.
void tamarin_parallel_for(int64_t start, int64_t end, int64_t grain_size,
                          tamarin_parallel_worker_fn worker, void* ctx);

/// Begins a structured task scope tracking frame.
void* tamarin_task_scope_begin(void);

/// Spawns an asynchronous task bound to an active structured task scope.
void tamarin_task_spawn(void* scope_handle, tamarin_task_fn task, void* ctx);

/// Blocks until all tasks within the structured scope complete, then cleans up.
void tamarin_task_scope_end(void* scope_handle);

/// Creates an asynchronous task executing fn with captured ctx_data.
/// Returns an in-flight task handle. Zero heap allocation when ctx_size and result_size <= 128 bytes.
void* tamarin_async_task_create(tamarin_async_fn fn, const void* ctx_data, int64_t ctx_size, int64_t result_size);

/// Awaits completion of an asynchronous task, cooperatively stealing tasks while waiting.
/// Returns a pointer to the computed result.
void* tamarin_async_task_await(void* task_handle);

/// Returns 1 if the asynchronous task has completed, 0 otherwise.
int32_t tamarin_async_task_is_ready(void* task_handle);

/// Frees/recycles an asynchronous task handle back to the thread-local pool.
void tamarin_async_task_free(void* task_handle);

/// Relinquishes execution slice to another ready thread or fiber.
void tamarin_yield(void);

/// Returns the number of worker threads active in the runtime thread pool.
int32_t tamarin_concurrency_get_num_threads(void);

#ifdef __cplusplus
}
#endif
