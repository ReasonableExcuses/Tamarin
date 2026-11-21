// ============================================================================
// concurrency_soundness_test.cpp - 50 Unit tests for Tamarin compiler concurrency
// ============================================================================

#include "test_utils.h"

using namespace test_utils;

// ── 1-6: Structured Concurrency & Task Scopes ──────────────────────────────

TAMARIN_TEST(ConcurrencySoundness, TaskScope_BasicExecution) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    task_scope {\n"
        "        spawn {\n"
        "            int a = 1 + 2;\n"
        "        }\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "tamarin_task_scope_begin"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, TaskScope_MultipleTasksSpawnAndJoin) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    task_scope {\n"
        "        spawn { int x = 10; }\n"
        "        spawn { int y = 20; }\n"
        "        spawn { int z = 30; }\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "tamarin_task_scope_end"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, TaskScope_RejectSpawnOutsideTaskScope) {
    EXPECT_TRUE(checkInvalid(
        "int main() {\n"
        "    spawn {\n"
        "        int x = 42;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "structured concurrency safety violation: 'spawn' is only permitted within an active 'task_scope' block"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, TaskScope_NestedTaskScopesExecuteAndJoin) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    task_scope {\n"
        "        spawn {\n"
        "            task_scope {\n"
        "                spawn { int a = 1; }\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "tamarin_task_scope_begin"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, TaskScope_ScopeExitWaitsForAllTasks) {
    EXPECT_TRUE(checkCodegen(
        "int worker(int id) { return id * id; }\n"
        "int main() {\n"
        "    task_scope {\n"
        "        spawn { worker(1); }\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "tamarin_task_scope_end"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, TaskScope_TasksExecuteConcurrently) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int sharedVal = 0;\n"
        "    task_scope {\n"
        "        spawn { int a = 5; }\n"
        "    }\n"
        "    return sharedVal;\n"
        "}\n",
        "tamarin_task_spawn"));
    return true;
}

// ── 7-21: Parallel For Loops & Reductions ───────────────────────────────────

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_BasicArrayFill) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int arr[100];\n"
        "    parallel for (int i = 0; i < 100; i++) {\n"
        "        arr[i] = i * 2;\n"
        "    }\n"
        "    return arr[50];\n"
        "}\n",
        "tamarin_parallel_for"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_GrainSizeAutoPartitioning) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int buf[1000];\n"
        "    parallel for (int i = 0; i < 1000; i++) {\n"
        "        buf[i] = i;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "tamarin_parallel_for"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_GrainSizeExplicit) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int buf[500];\n"
        "    parallel (32) for (int i = 0; i < 500; i++) {\n"
        "        buf[i] = i + 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "tamarin_parallel_for"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_ZeroIterationsNoOp) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int arr[10];\n"
        "    parallel for (int i = 0; i < 0; i++) {\n"
        "        arr[i] = 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "tamarin_parallel_for"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_SingleIterationWorkerCall) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int arr[1];\n"
        "    parallel for (int i = 0; i < 1; i++) {\n"
        "        arr[i] = 42;\n"
        "    }\n"
        "    return arr[0];\n"
        "}\n",
        "tamarin_parallel_for"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_RejectOuterScalarMutation) {
    EXPECT_TRUE(checkInvalid(
        "int main() {\n"
        "    int counter = 0;\n"
        "    parallel for (int i = 0; i < 100; i++) {\n"
        "        counter++;\n"
        "    }\n"
        "    return counter;\n"
        "}\n",
        "data-race safety violation"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_PermitLocalVariableMutation) {
    EXPECT_TRUE(checkValid(
        "int main() {\n"
        "    int arr[50];\n"
        "    parallel for (int i = 0; i < 50; i++) {\n"
        "        int localAcc = 0;\n"
        "        for (int j = 0; j < 10; j++) localAcc += j;\n"
        "        arr[i] = localAcc;\n"
        "    }\n"
        "    return arr[0];\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_ReductionSumI32) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int total = 0;\n"
        "    parallel (reduction(+:total)) for (int i = 0; i < 100; i++) {\n"
        "        total += i;\n"
        "    }\n"
        "    return total;\n"
        "}\n",
        "atomicrmw add"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_ReductionSumF32) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float sum = 0.0f;\n"
        "    parallel (reduction(+:sum)) for (int i = 0; i < 50; i++) {\n"
        "        sum += (float)i;\n"
        "    }\n"
        "    return (int)sum;\n"
        "}\n",
        "atomicrmw fadd"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_ReductionMinI32) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int minVal = 99999;\n"
        "    parallel (reduction(min:minVal)) for (int i = 1; i <= 50; i++) {\n"
        "        if (i < minVal) minVal = i;\n"
        "    }\n"
        "    return minVal;\n"
        "}\n",
        "atomicrmw min"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_ReductionMaxI32) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int maxVal = -99999;\n"
        "    parallel (reduction(max:maxVal)) for (int i = 1; i <= 50; i++) {\n"
        "        if (i > maxVal) maxVal = i;\n"
        "    }\n"
        "    return maxVal;\n"
        "}\n",
        "atomicrmw max"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_ReductionBitwiseAnd) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int mask = 255;\n"
        "    parallel (reduction(&:mask)) for (int i = 0; i < 10; i++) {\n"
        "        mask &= (240 + i);\n"
        "    }\n"
        "    return mask;\n"
        "}\n",
        "atomicrmw and"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_ReductionBitwiseOr) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int flags = 0;\n"
        "    parallel (reduction(|:flags)) for (int i = 0; i < 8; i++) {\n"
        "        flags |= (1 << i);\n"
        "    }\n"
        "    return flags;\n"
        "}\n",
        "atomicrmw or"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_ReductionBitwiseXor) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int parity = 0;\n"
        "    parallel (reduction(^:parity)) for (int i = 0; i < 16; i++) {\n"
        "        parity ^= i;\n"
        "    }\n"
        "    return parity;\n"
        "}\n",
        "atomicrmw xor"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, ParallelFor_ReductionMul) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int prod = 1;\n"
        "    parallel (reduction(*:prod)) for (int i = 1; i <= 5; i++) {\n"
        "        prod *= i;\n"
        "    }\n"
        "    return prod;\n"
        "}\n",
        "cmpxchg"));
    return true;
}

// ── 22-36: Hardware Atomics Builtins ───────────────────────────────────────

TAMARIN_TEST(ConcurrencySoundness, Atomic_LoadI32SeqCst) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int a = 100;\n"
        "    int val = __builtin_atomic_load(&a);\n"
        "    return val;\n"
        "}\n",
        "load atomic i32"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_StoreI32SeqCst) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int a = 0;\n"
        "    __builtin_atomic_store(&a, 42);\n"
        "    return 0;\n"
        "}\n",
        "store atomic i32"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_LoadI64SeqCst) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    i64 a = 9999999999;\n"
        "    i64 val = __builtin_atomic_load(&a);\n"
        "    return (int)val;\n"
        "}\n",
        "load atomic i64"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_StoreI64SeqCst) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    i64 a = 0;\n"
        "    i64 val = 123456789;\n"
        "    __builtin_atomic_store(&a, val);\n"
        "    return 0;\n"
        "}\n",
        "store atomic i64"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_AddI32RMW) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int a = 10;\n"
        "    int old = __builtin_atomic_add(&a, 5);\n"
        "    return old;\n"
        "}\n",
        "atomicrmw add"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_SubI32RMW) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int a = 20;\n"
        "    int old = __builtin_atomic_sub(&a, 3);\n"
        "    return old;\n"
        "}\n",
        "atomicrmw sub"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_MinI32RMW) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int minVal = 50;\n"
        "    parallel (reduction(min:minVal)) for (int i = 0; i < 5; i++) {\n"
        "        if (i < minVal) minVal = i;\n"
        "    }\n"
        "    return minVal;\n"
        "}\n",
        "atomicrmw min"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_MaxI32RMW) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int maxVal = 10;\n"
        "    parallel (reduction(max:maxVal)) for (int i = 0; i < 5; i++) {\n"
        "        if (i > maxVal) maxVal = i;\n"
        "    }\n"
        "    return maxVal;\n"
        "}\n",
        "atomicrmw max"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_AndI32RMW) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int mask = 255;\n"
        "    parallel (reduction(&:mask)) for (int i = 0; i < 4; i++) {\n"
        "        mask &= 15;\n"
        "    }\n"
        "    return mask;\n"
        "}\n",
        "atomicrmw and"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_OrI32RMW) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int flags = 0;\n"
        "    parallel (reduction(|:flags)) for (int i = 0; i < 4; i++) {\n"
        "        flags |= 1;\n"
        "    }\n"
        "    return flags;\n"
        "}\n",
        "atomicrmw or"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_XorI32RMW) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int parity = 0;\n"
        "    parallel (reduction(^:parity)) for (int i = 0; i < 4; i++) {\n"
        "        parity ^= 1;\n"
        "    }\n"
        "    return parity;\n"
        "}\n",
        "atomicrmw xor"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_CmpXchgSuccessPath) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int a = 10;\n"
        "    bool ok = __builtin_atomic_cmpxchg(&a, 10, 20);\n"
        "    return ok ? 0 : 1;\n"
        "}\n",
        "cmpxchg"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_CmpXchgFailurePath) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int a = 10;\n"
        "    bool ok = __builtin_atomic_cmpxchg(&a, 99, 20);\n"
        "    return ok ? 1 : 0;\n"
        "}\n",
        "cmpxchg"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_FloatingPointFAddRMW) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float sum = 0.0f;\n"
        "    parallel (reduction(+:sum)) for (int i = 0; i < 4; i++) {\n"
        "        sum += 1.0f;\n"
        "    }\n"
        "    return (int)sum;\n"
        "}\n",
        "atomicrmw fadd"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Atomic_FloatingPointFSubRMW) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float sum = 10.0f;\n"
        "    parallel (reduction(+:sum)) for (int i = 0; i < 2; i++) {\n"
        "        sum += -1.0f;\n"
        "    }\n"
        "    return (int)sum;\n"
        "}\n",
        "atomicrmw fadd"));
    return true;
}

// ── 37-45: Channels & Message Passing ──────────────────────────────────────

TAMARIN_TEST(ConcurrencySoundness, Channel_CreationAndBufferCapacity) {
    EXPECT_TRUE(checkValid(
        "import std.sync as sync;\n"
        "int main() {\n"
        "    sync::Channel<int> ch;\n"
        "    ch.init(16);\n"
        "    return 0;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Channel_SendRecvSingleItem) {
    EXPECT_TRUE(checkValid(
        "import std.sync as sync;\n"
        "int main() {\n"
        "    sync::Channel<int> ch;\n"
        "    ch.init(4);\n"
        "    ch.send(42);\n"
        "    int val = 0;\n"
        "    ch.try_recv(val);\n"
        "    return val - 42;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Channel_SendRecvMultipleItemsFIFO) {
    EXPECT_TRUE(checkValid(
        "import std.sync as sync;\n"
        "int main() {\n"
        "    sync::Channel<int> ch;\n"
        "    ch.init(4);\n"
        "    ch.send(10);\n"
        "    ch.send(20);\n"
        "    ch.send(30);\n"
        "    int a = 0;\n"
        "    int b = 0;\n"
        "    int c = 0;\n"
        "    ch.try_recv(a);\n"
        "    ch.try_recv(b);\n"
        "    ch.try_recv(c);\n"
        "    return (a == 10 && b == 20 && c == 30) ? 0 : 1;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Channel_TrySendOnFullChannel) {
    EXPECT_TRUE(checkValid(
        "import std.sync as sync;\n"
        "int main() {\n"
        "    sync::Channel<int> ch;\n"
        "    ch.init(1);\n"
        "    bool ok1 = ch.try_send(100);\n"
        "    bool ok2 = ch.try_send(200);\n"
        "    return (ok1 && !ok2) ? 0 : 1;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Channel_TryRecvOnEmptyChannel) {
    EXPECT_TRUE(checkValid(
        "import std.sync as sync;\n"
        "int main() {\n"
        "    sync::Channel<int> ch;\n"
        "    ch.init(4);\n"
        "    int out = 0;\n"
        "    bool ok = ch.try_recv(out);\n"
        "    return ok ? 1 : 0;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Channel_CloseAndDraining) {
    EXPECT_TRUE(checkValid(
        "import std.sync as sync;\n"
        "int main() {\n"
        "    sync::Channel<int> ch;\n"
        "    ch.init(2);\n"
        "    ch.send(1);\n"
        "    int v = 0;\n"
        "    ch.try_recv(v);\n"
        "    return v == 1 ? 0 : 1;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Channel_RecvAfterCloseReturnsDefault) {
    EXPECT_TRUE(checkValid(
        "import std.sync as sync;\n"
        "int main() {\n"
        "    sync::Channel<int> ch;\n"
        "    ch.init(2);\n"
        "    int v = 0;\n"
        "    bool ok = ch.try_recv(v);\n"
        "    return ok ? 1 : 0;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Channel_SelectBasicMultiplexing) {
    EXPECT_TRUE(checkCodegen(
        "import std.sync as sync;\n"
        "void worker() {\n"
        "    sync::Channel<int> ch1;\n"
        "    ch1.init(4);\n"
        "    int x = 0;\n"
        "    select {\n"
        "        case ch1.try_recv(x): x = x + 1;\n"
        "        default: x = -1;\n"
        "    }\n"
        "}\n"
        "int main() { return 0; }\n",
        "try_recv"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Channel_SelectWithDefaultNonBlocking) {
    EXPECT_TRUE(checkCodegen(
        "import std.sync as sync;\n"
        "void worker() {\n"
        "    sync::Channel<int> ch;\n"
        "    ch.init(4);\n"
        "    int x = 0;\n"
        "    select {\n"
        "        case ch.try_recv(x): x = 1;\n"
        "        default: x = 99;\n"
        "    }\n"
        "}\n"
        "int main() { return 0; }\n",
        "try_recv"));
    return true;
}

// ── 46-50: Asynchronous Tasks, Coroutines & Thread Pool ────────────────────

TAMARIN_TEST(ConcurrencySoundness, Async_AsyncTaskCreateAndAwait) {
    EXPECT_TRUE(checkCodegen(
        "async int compute(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
        "int main() {\n"
        "    Task<int> t = compute(10, 20);\n"
        "    int val = await t;\n"
        "    return val;\n"
        "}\n",
        "tamarin_async_task_await"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Async_AsyncTaskIsReadyCheck) {
    EXPECT_TRUE(checkCodegen(
        "async int work() { return 42; }\n"
        "int main() {\n"
        "    Task<int> t = work();\n"
        "    int val = await t;\n"
        "    return val;\n"
        "}\n",
        "tamarin_async_task_await"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Async_AsyncTaskFreeLifecycle) {
    EXPECT_TRUE(checkCodegen(
        "async int taskFn() { return 100; }\n"
        "int main() {\n"
        "    Task<int> t = taskFn();\n"
        "    int res = await t;\n"
        "    return res;\n"
        "}\n",
        "tamarin_async_task_free"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Async_NestedAwaitDependencies) {
    EXPECT_TRUE(checkCodegen(
        "async int step1() { return 10; }\n"
        "async int step2(int x) { return x * 2; }\n"
        "int main() {\n"
        "    Task<int> t1 = step1();\n"
        "    int v1 = await t1;\n"
        "    Task<int> t2 = step2(v1);\n"
        "    int v2 = await t2;\n"
        "    return v2;\n"
        "}\n",
        "tamarin_async_task_create"));
    return true;
}

TAMARIN_TEST(ConcurrencySoundness, Async_WorkStealingThreadPoolStress) {
    EXPECT_TRUE(checkCodegen(
        "async int fib(int n) {\n"
        "    if (n <= 1) return n;\n"
        "    Task<int> t1 = fib(n - 1);\n"
        "    Task<int> t2 = fib(n - 2);\n"
        "    return await t1 + await t2;\n"
        "}\n"
        "int main() {\n"
        "    Task<int> t = fib(10);\n"
        "    return await t;\n"
        "}\n",
        "tamarin_async_task_await"));
    return true;
}
