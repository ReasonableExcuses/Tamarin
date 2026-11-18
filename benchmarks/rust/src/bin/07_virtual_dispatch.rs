// 07_virtual_dispatch.rs - OOP Direct vs Virtual Dispatch Benchmark
// In Rust, trait objects (`&dyn Shape`) represent dynamic vtable dispatch

trait Shape {
    fn area(&self) -> i64;
}

struct Rectangle {
    w: i64,
    h: i64,
}

impl Rectangle {
    fn direct_area(&self) -> i64 {
        self.w * self.h
    }
}

impl Shape for Rectangle {
    fn area(&self) -> i64 {
        self.w * self.h
    }
}

struct Triangle {
    b: i64,
    h: i64,
}

impl Triangle {
    fn direct_area(&self) -> i64 {
        (self.b * self.h) / 2
    }
}

impl Shape for Triangle {
    fn area(&self) -> i64 {
        (self.b * self.h) / 2
    }
}

#[inline(never)]
fn call_virtual(s: &dyn Shape) -> i64 {
    s.area()
}

fn main() {
    let rect = Rectangle { w: 10, h: 20 };
    let tri = Triangle { b: 15, h: 30 };

    let start = std::time::Instant::now();

    // 1. Direct dispatch loop (2,000,000 iterations)
    let mut direct_sum: i64 = 0;
    for _ in 0..2000000 {
        direct_sum += rect.direct_area();
        direct_sum += tri.direct_area();
    }

    // 2. Virtual dispatch loop (2,000,000 iterations via &dyn Shape)
    let mut virtual_sum: i64 = 0;
    for _ in 0..2000000 {
        virtual_sum += call_virtual(&rect);
        virtual_sum += call_virtual(&tri);
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

    let checksum = direct_sum + virtual_sum;
    println!("TIME_MS:{:.4};DIRECT:{};VIRTUAL:{};CHECKSUM:{}",
             elapsed_ms, direct_sum, virtual_sum, checksum);
}
