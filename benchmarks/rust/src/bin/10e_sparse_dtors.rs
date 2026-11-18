// 10e_sparse_dtors.rs - Pattern E: Regions with sparse destructor sidecars

use tamarin_benchmarks_rust::{Arena, Zeroable};

#[derive(Clone, Copy)]
struct Token {
    id: i64,
}

unsafe impl Zeroable for Token {}

fn main() {
    let start = std::time::Instant::now();

    let mut sum: i64 = 0;
    for iter in 0..1000 {
        let r = Arena::new(65536);
        for i in 0..4 {
            let t = r.alloc::<Token>(1, 8);
            t[0].id = (iter * 4 + i) as i64;
            let ptr = &mut t[0].id as *mut i64;
            r.register_dtor(move || {
                unsafe {
                    let cur = std::ptr::read_volatile(ptr);
                    std::ptr::write_volatile(ptr, 0 - cur);
                }
            });
            sum += t[0].id;
        }
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

    println!("TIME_MS:{:.4};SUM:{};CHECKSUM:{}",
             elapsed_ms, sum, sum);
}
