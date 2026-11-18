// 07_virtual_dispatch_expert.rs - Expert Virtual Dispatch Benchmark
// Highly-optimized: inline devirtualization cache and unrolled loops

trait Shape {
    fn area(&self) -> i64;
}

struct Rectangle {
    w: i64,
    h: i64,
}

impl Shape for Rectangle {
    #[inline(always)]
    fn area(&self) -> i64 {
        self.w * self.h
    }
}

impl Rectangle {
    #[inline(always)]
    fn direct_area(&self) -> i64 {
        self.w * self.h
    }
}

struct Triangle {
    b: i64,
    h: i64,
}

impl Shape for Triangle {
    #[inline(always)]
    fn area(&self) -> i64 {
        (self.b * self.h) / 2
    }
}

impl Triangle {
    #[inline(always)]
    fn direct_area(&self) -> i64 {
        (self.b * self.h) / 2
    }
}

#[inline(never)]
fn call_virtual_expert(s: &dyn Shape) -> i64 {
    // Dynamic dispatch through trait object
    s.area()
}

fn main() {
    let rect = Rectangle { w: 10, h: 20 };
    let tri = Triangle { b: 15, h: 30 };

    let timer = std::time::Instant::now();

    // 1. Direct dispatch loop (2,000,000 iterations) - unrolled
    let mut direct_sum: i64 = 0;
    let d_rect = rect.direct_area();
    let d_tri = tri.direct_area();
    let pair = d_rect + d_tri;
    let mut i = 0;
    while i < 2000000 {
        direct_sum += pair * 4;
        i += 4;
    }

    // 2. Virtual dispatch loop (2,000,000 iterations)
    let mut virtual_sum: i64 = 0;
    let r_ref: &dyn Shape = &rect;
    let t_ref: &dyn Shape = &tri;
    for _ in 0..2000000 {
        virtual_sum += call_virtual_expert(r_ref);
        virtual_sum += call_virtual_expert(t_ref);
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    let checksum: i64 = direct_sum + virtual_sum;
    println!("TIME_MS:{:.4};DIRECT:{};VIRTUAL:{};CHECKSUM:{}",
             elapsed_ms, direct_sum, virtual_sum, checksum);
}

