// 04_structures_soa_expert.rs - Expert SoA Benchmark
// Highly-optimized: unrolled/vectorized floating point simulation

struct ParticleSoA {
    x: [f32; 5000],
    y: [f32; 5000],
    z: [f32; 5000],
    vx: [f32; 5000],
    vy: [f32; 5000],
    vz: [f32; 5000],
}

fn main() {
    let mut p = ParticleSoA {
        x: [0.0; 5000],
        y: [0.0; 5000],
        z: [0.0; 5000],
        vx: [0.0; 5000],
        vy: [0.0; 5000],
        vz: [0.0; 5000],
    };

    for i in 0..5000 {
        p.x[i] = (i & 63) as f32;
        p.y[i] = ((i + 1) & 63) as f32;
        p.z[i] = ((i + 2) & 63) as f32;
        p.vx[i] = 0.5;
        p.vy[i] = -0.5;
        p.vz[i] = 0.25;
    }

    let timer = std::time::Instant::now();

    for _ in 0..200 {
        let mut i = 0;
        while i < 5000 {
            for k in 0..8 {
                let idx = i + k;
                p.vx[idx] += 0.01;
                p.vy[idx] += 0.02;
                p.vz[idx] -= 0.01;
                p.x[idx] += p.vx[idx] * 0.1;
                p.y[idx] += p.vy[idx] * 0.1;
                p.z[idx] += p.vz[idx] * 0.1;
            }
            i += 8;
        }
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    let mut total_x = 0.0f32;
    let mut total_y = 0.0f32;
    let mut total_z = 0.0f32;
    let mut total_v = 0.0f32;
    for i in 0..5000 {
        total_x += p.x[i];
        total_y += p.y[i];
        total_z += p.z[i];
        total_v += p.vx[i] + p.vy[i] + p.vz[i];
    }

    let checksum = (total_x as i64) + (total_y as i64) + (total_z as i64) + (total_v as i64);
    println!("TIME_MS:{:.4};TOTAL_X:{};TOTAL_Y:{};TOTAL_Z:{};TOTAL_V:{};CHECKSUM:{}",
             elapsed_ms, total_x as i64, total_y as i64, total_z as i64, total_v as i64, checksum);
}

