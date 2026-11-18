// 04_structures_soa.rs - SoA (Structure of Arrays) Benchmark

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
        x: [0.0f32; 5000],
        y: [0.0f32; 5000],
        z: [0.0f32; 5000],
        vx: [0.0f32; 5000],
        vy: [0.0f32; 5000],
        vz: [0.0f32; 5000],
    };

    // Initialize outside timed section
    for i in 0..5000 {
        p.x[i] = (i & 63) as f32;
        p.y[i] = ((i + 1) & 63) as f32;
        p.z[i] = ((i + 2) & 63) as f32;
        p.vx[i] = 0.5f32;
        p.vy[i] = -0.5f32;
        p.vz[i] = 0.25f32;
    }

    let start = std::time::Instant::now();

    // Timed simulation steps
    for _ in 0..200 {
        for i in 0..5000 {
            p.vx[i] = p.vx[i] + 0.01f32;
            p.vy[i] = p.vy[i] + 0.02f32;
            p.vz[i] = p.vz[i] - 0.01f32;

            p.x[i] = p.x[i] + p.vx[i] * 0.1f32;
            p.y[i] = p.y[i] + p.vy[i] * 0.1f32;
            p.z[i] = p.z[i] + p.vz[i] * 0.1f32;
        }
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

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

    let checksum: i64 = (total_x as i64) + (total_y as i64) + (total_z as i64) + (total_v as i64);
    println!("TIME_MS:{:.4};TOTAL_X:{};TOTAL_Y:{};TOTAL_Z:{};TOTAL_V:{};CHECKSUM:{}",
             elapsed_ms, total_x as i64, total_y as i64, total_z as i64, total_v as i64, checksum);
}
