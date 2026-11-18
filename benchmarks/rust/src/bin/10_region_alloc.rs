// 10_region_alloc.rs - Memory Regions and Monotonic Arena Benchmark
// Tests monotonic bump allocation, nested regions, and bulk reclamation

use tamarin_benchmarks_rust::{Arena, Zeroable};

#[derive(Clone, Copy, Default)]
struct Particle {
    id: i64,
    x: i64,
    y: i64,
    z: i64,
}

unsafe impl Zeroable for Particle {}

fn main() {
    let start = std::time::Instant::now();

    let mut particle_sum: i64 = 0;
    let mut scratch_sum: i64 = 0;
    let mut nested_sum: i64 = 0;

    for iter in 0..5000 {
        let frame = Arena::new(65536);
        let particles = frame.alloc::<Particle>(100, 8);
        for i in 0..100 {
            particles[i].id = (iter * 100 + i) as i64;
            particles[i].x = (i * 3 + iter) as i64;
            particles[i].y = (i * 7 + iter * 2) as i64;
            particles[i].z = (i * 11 + iter * 3) as i64;
        }

        let scratch = frame.alloc::<i32>(200, 4);
        for j in 0..100 {
            scratch[j] = (particles[j].x + particles[j].y) as i32;
            scratch[j + 100] = (particles[j].z - particles[j].x) as i32;
        }

        let growth = frame.alloc::<i32>(18000, 4);
        growth[0] = scratch[0] + (particles[0].x as i32);
        growth[17999] = scratch[199] + (particles[99].z as i32);

        for k in 0..200 {
            scratch_sum += scratch[k] as i64;
        }
        scratch_sum += (growth[0] + growth[17999]) as i64;

        for p in 0..100 {
            particle_sum += particles[p].id;
        }

        let subframe = Arena::new(16384);
        let temp = subframe.alloc::<i32>(50, 4);
        for m in 0..50 {
            temp[m] = scratch[m] + (particles[m].z as i32);
            nested_sum += temp[m] as i64;
        }
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;
    let checksum = particle_sum + scratch_sum + nested_sum;

    println!("TIME_MS:{:.4};PARTICLES:{};SCRATCH:{};NESTED:{};CHECKSUM:{}",
             elapsed_ms, particle_sum, scratch_sum, nested_sum, checksum);
}
