// 10e_sparse_dtors_expert.rs - Expert Pattern E: Regions with sparse destructor sidecars

use tamarin_benchmarks_rust::{PooledArena, Zeroable};

#[derive(Clone, Copy)]
#[repr(C)]
struct Token {
    id: i64,
}

unsafe impl Zeroable for Token {}

fn main() {
    let timer = std::time::Instant::now();

    let mut sum: i64 = 0;
    for iter in 0..1000 {
        let r = PooledArena::new(4096);

        let t0 = &mut r.alloc::<Token>(1, 8)[0];
        let t1 = &mut r.alloc::<Token>(1, 8)[0];
        let t2 = &mut r.alloc::<Token>(1, 8)[0];
        let t3 = &mut r.alloc::<Token>(1, 8)[0];

        t0.id = (iter * 4 + 0) as i64;
        t1.id = (iter * 4 + 1) as i64;
        t2.id = (iter * 4 + 2) as i64;
        t3.id = (iter * 4 + 3) as i64;

        sum += t0.id + t1.id + t2.id + t3.id;

        // Register destructors (executed in LIFO reverse order on drop)
        let ptr0 = &mut t0.id as *mut i64;
        let ptr1 = &mut t1.id as *mut i64;
        let ptr2 = &mut t2.id as *mut i64;
        let ptr3 = &mut t3.id as *mut i64;
        r.register_dtor(move || unsafe { std::ptr::write_volatile(ptr0, 0 - std::ptr::read_volatile(ptr0)); });
        r.register_dtor(move || unsafe { std::ptr::write_volatile(ptr1, 0 - std::ptr::read_volatile(ptr1)); });
        r.register_dtor(move || unsafe { std::ptr::write_volatile(ptr2, 0 - std::ptr::read_volatile(ptr2)); });
        r.register_dtor(move || unsafe { std::ptr::write_volatile(ptr3, 0 - std::ptr::read_volatile(ptr3)); });
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    println!("TIME_MS:{:.4};SUM:{};CHECKSUM:{}",
             elapsed_ms, sum, sum);
}

