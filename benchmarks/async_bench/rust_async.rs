// benchmarks/async_bench/rust_async.rs
// Rust async/await benchmark: High-throughput task churn & recursive tree reduction

use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};
use std::time::Instant;

fn dummy_waker() -> Waker {
    unsafe fn clone(_: *const ()) -> RawWaker { RawWaker::new(std::ptr::null(), &VTABLE) }
    unsafe fn wake(_: *const ()) {}
    unsafe fn wake_by_ref(_: *const ()) {}
    unsafe fn drop(_: *const ()) {}
    static VTABLE: RawWakerVTable = RawWakerVTable::new(clone, wake, wake_by_ref, drop);
    unsafe { Waker::from_raw(RawWaker::new(std::ptr::null(), &VTABLE)) }
}

fn block_on<F: Future>(mut future: F) -> F::Output {
    let waker = dummy_waker();
    let mut cx = Context::from_waker(&waker);
    // Pin to stack
    let mut future = unsafe { Pin::new_unchecked(&mut future) };
    loop {
        match future.as_mut().poll(&mut cx) {
            Poll::Ready(output) => return output,
            Poll::Pending => std::thread::yield_now(),
        }
    }
}

async fn task_work(x: i32) -> i32 {
    x * 3 + 1
}

fn tree_sum(depth: i32, val: i32) -> Pin<Box<dyn Future<Output = i32>>> {
    Box::pin(async move {
        if depth <= 0 {
            return val;
        }
        let left = tree_sum(depth - 1, val * 2);
        let right = tree_sum(depth - 1, val * 2 + 1);
        let l = left.await;
        let r = right.await;
        l + r
    })
}

fn main() {
    let start = Instant::now();

    // 1. High-frequency async task churn (50,000 tasks)
    let mut churn_sum: i32 = 0;
    for i in 0..50000 {
        let res = block_on(task_work(i));
        churn_sum += res;
    }

    // 2. Recursive tree reduction depth 14 (16,383 nodes)
    let tree_res = block_on(tree_sum(14, 1));

    let elapsed = start.elapsed().as_secs_f64() * 1000.0;
    let checksum = (churn_sum as i64) + (tree_res as i64);

    println!("TIME_MS: {:.3}", elapsed);
    println!("CHECKSUM: {}", checksum);
}
