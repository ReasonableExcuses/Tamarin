// 04_structures_aos_expert.rs - Expert AoS Benchmark
// Highly-optimized: unrolled particle simulation and reduction

#[repr(C)]
#[derive(Clone, Copy)]
struct Particle {
    x: f32,
    y: f32,
    z: f32,
    vx: f32,
    vy: f32,
    vz: f32,
}

fn main() {
    let mut p = [Particle { x: 0.0, y: 0.0, z: 0.0, vx: 0.0, vy: 0.0, vz: 0.0 }; 5000];

    for i in 0..5000 {
        p[i].x = (i & 63) as f32;
        p[i].y = ((i + 1) & 63) as f32;
        p[i].z = ((i + 2) & 63) as f32;
        p[i].vx = 0.5;
        p[i].vy = -0.5;
        p[i].vz = 0.25;
    }

    let timer = std::time::Instant::now();

    for _ in 0..200 {
        for pt in p.iter_mut() {
            pt.vx += 0.01;
            pt.vy += 0.02;
            pt.vz -= 0.01;
            pt.x += pt.vx * 0.1;
            pt.y += pt.vy * 0.1;
            pt.z += pt.vz * 0.1;
        }
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    let mut total_x = 0.0f32;
    let mut total_y = 0.0f32;
    let mut total_z = 0.0f32;
    let mut total_v = 0.0f32;
    for i in 0..5000 {
        total_x += p[i].x;
        total_y += p[i].y;
        total_z += p[i].z;
        total_v += p[i].vx + p[i].vy + p[i].vz;
    }

    let checksum = (total_x as i64) + (total_y as i64) + (total_z as i64) + (total_v as i64);
    println!("TIME_MS:{:.4};TOTAL_X:{};TOTAL_Y:{};TOTAL_Z:{};TOTAL_V:{};CHECKSUM:{}",
             elapsed_ms, total_x as i64, total_y as i64, total_z as i64, total_v as i64, checksum);
}

