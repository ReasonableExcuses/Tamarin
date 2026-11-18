// 13_nbody_simulation - Rust Normal (Vec<Particle> AoS)
// 4,096 bodies, 20 time steps = 335,544,320 pairwise interactions

use std::time::Instant;

#[derive(Clone, Copy)]
struct Particle {
    x: f32,
    y: f32,
    z: f32,
    vx: f32,
    vy: f32,
    vz: f32,
    mass: f32,
}

fn main() {
    const N: usize = 4096;
    let mut p = vec![Particle { x: 0.0, y: 0.0, z: 0.0, vx: 0.0, vy: 0.0, vz: 0.0, mass: 0.0 }; N];
    let mut seed: i64 = 123456789;

    // Deterministic LCG initialization outside timed kernel
    for i in 0..N {
        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        p[i].x = ((seed & 65535) as f32 / 65535.0) * 20.0 - 10.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        p[i].y = ((seed & 65535) as f32 / 65535.0) * 20.0 - 10.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        p[i].z = ((seed & 65535) as f32 / 65535.0) * 20.0 - 10.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        p[i].vx = ((seed & 65535) as f32 / 65535.0) * 2.0 - 1.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        p[i].vy = ((seed & 65535) as f32 / 65535.0) * 2.0 - 1.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        p[i].vz = ((seed & 65535) as f32 / 65535.0) * 2.0 - 1.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        p[i].mass = ((seed & 65535) as f32 / 65535.0) * 9.9 + 0.1;
    }

    let dt: f32 = 0.01;
    let softening: f32 = 0.01;

    let start = Instant::now();

    // 20 simulation time steps
    for _step in 0..20 {
        for i in 0..N {
            let px = p[i].x;
            let py = p[i].y;
            let pz = p[i].z;
            let mut ax: f32 = 0.0;
            let mut ay: f32 = 0.0;
            let mut az: f32 = 0.0;

            for j in 0..N {
                let dx = p[j].x - px;
                let dy = p[j].y - py;
                let dz = p[j].z - pz;
                let dist_sqr = dx * dx + dy * dy + dz * dz + softening;
                let inv_dist = 1.0 / dist_sqr.sqrt();
                let inv_dist3 = inv_dist * inv_dist * inv_dist;
                let s = p[j].mass * inv_dist3;
                ax += dx * s;
                ay += dy * s;
                az += dz * s;
            }

            p[i].vx += ax * dt;
            p[i].vy += ay * dt;
            p[i].vz += az * dt;

            p[i].x = px + p[i].vx * dt;
            p[i].y = py + p[i].vy * dt;
            p[i].z = pz + p[i].vz * dt;
        }
    }

    let elapsed = start.elapsed();
    let elapsed_ms = elapsed.as_secs_f64() * 1000.0;

    let mut total_x: f32 = 0.0;
    let mut total_y: f32 = 0.0;
    let mut total_z: f32 = 0.0;
    let mut total_v: f32 = 0.0;
    let mut total_ek: f32 = 0.0;

    for i in 0..N {
        total_x += p[i].x;
        total_y += p[i].y;
        total_z += p[i].z;
        total_v += p[i].vx + p[i].vy + p[i].vz;
        let vsq = p[i].vx * p[i].vx + p[i].vy * p[i].vy + p[i].vz * p[i].vz;
        total_ek += 0.5 * p[i].mass * vsq;
    }

    let checksum: i64 = (total_x as i64) + (total_y as i64) + (total_z as i64) +
                        (total_v as i64) + (total_ek as i64);

    println!(
        "TIME_MS:{:.4};TOTAL_X:{};TOTAL_Y:{};TOTAL_Z:{};TOTAL_V:{};TOTAL_EK:{};CHECKSUM:{}",
        elapsed_ms,
        total_x as i64,
        total_y as i64,
        total_z as i64,
        total_v as i64,
        total_ek as i64,
        checksum
    );
}
