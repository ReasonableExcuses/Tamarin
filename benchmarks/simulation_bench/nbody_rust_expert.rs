// 13_nbody_simulation - Rust Expert (Structure-of-Arrays SoA)
// 4,096 bodies, 20 time steps = 335,544,320 pairwise interactions

use std::time::Instant;

fn main() {
    const N: usize = 4096;

    let mut pos_x = vec![0.0f32; N];
    let mut pos_y = vec![0.0f32; N];
    let mut pos_z = vec![0.0f32; N];
    let mut vel_x = vec![0.0f32; N];
    let mut vel_y = vec![0.0f32; N];
    let mut vel_z = vec![0.0f32; N];
    let mut mass  = vec![0.0f32; N];

    let mut seed: i64 = 123456789;

    // Deterministic LCG initialization outside timed kernel
    for i in 0..N {
        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        pos_x[i] = ((seed & 65535) as f32 / 65535.0) * 20.0 - 10.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        pos_y[i] = ((seed & 65535) as f32 / 65535.0) * 20.0 - 10.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        pos_z[i] = ((seed & 65535) as f32 / 65535.0) * 20.0 - 10.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        vel_x[i] = ((seed & 65535) as f32 / 65535.0) * 2.0 - 1.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        vel_y[i] = ((seed & 65535) as f32 / 65535.0) * 2.0 - 1.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        vel_z[i] = ((seed & 65535) as f32 / 65535.0) * 2.0 - 1.0;

        seed = (seed.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        mass[i] = ((seed & 65535) as f32 / 65535.0) * 9.9 + 0.1;
    }

    let dt: f32 = 0.01;
    let softening: f32 = 0.01;

    let start = Instant::now();

    // 20 simulation time steps
    for _step in 0..20 {
        for i in 0..N {
            let px = unsafe { *pos_x.get_unchecked(i) };
            let py = unsafe { *pos_y.get_unchecked(i) };
            let pz = unsafe { *pos_z.get_unchecked(i) };
            let mut ax: f32 = 0.0;
            let mut ay: f32 = 0.0;
            let mut az: f32 = 0.0;

            for j in 0..N {
                let dx = unsafe { *pos_x.get_unchecked(j) } - px;
                let dy = unsafe { *pos_y.get_unchecked(j) } - py;
                let dz = unsafe { *pos_z.get_unchecked(j) } - pz;
                let dist_sqr = dx * dx + dy * dy + dz * dz + softening;
                let inv_dist = 1.0 / dist_sqr.sqrt();
                let inv_dist3 = inv_dist * inv_dist * inv_dist;
                let s = unsafe { *mass.get_unchecked(j) } * inv_dist3;
                ax += dx * s;
                ay += dy * s;
                az += dz * s;
            }

            unsafe {
                *vel_x.get_unchecked_mut(i) += ax * dt;
                *vel_y.get_unchecked_mut(i) += ay * dt;
                *vel_z.get_unchecked_mut(i) += az * dt;

                *pos_x.get_unchecked_mut(i) = px + *vel_x.get_unchecked(i) * dt;
                *pos_y.get_unchecked_mut(i) = py + *vel_y.get_unchecked(i) * dt;
                *pos_z.get_unchecked_mut(i) = pz + *vel_z.get_unchecked(i) * dt;
            }
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
        total_x += pos_x[i];
        total_y += pos_y[i];
        total_z += pos_z[i];
        total_v += vel_x[i] + vel_y[i] + vel_z[i];
        let vsq = vel_x[i] * vel_x[i] + vel_y[i] * vel_y[i] + vel_z[i] * vel_z[i];
        total_ek += 0.5 * mass[i] * vsq;
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
