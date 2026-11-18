// 13_nbody_simulation - Rust Expert AVX2 Intrinsics
// 4,096 bodies, 20 time steps = 335,544,320 pairwise interactions

use std::time::Instant;
#[cfg(target_arch = "x86_64")]
use std::arch::x86_64::*;

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

    unsafe {
        let soft_vec = _mm256_set1_ps(softening);
        let c_one_point_five = _mm256_set1_ps(1.5);
        let c_half = _mm256_set1_ps(0.5);

        for _step in 0..20 {
            for i in 0..N {
                let px = *pos_x.get_unchecked(i);
                let py = *pos_y.get_unchecked(i);
                let pz = *pos_z.get_unchecked(i);

                let vpx = _mm256_set1_ps(px);
                let vpy = _mm256_set1_ps(py);
                let vpz = _mm256_set1_ps(pz);

                let mut vax = _mm256_setzero_ps();
                let mut vay = _mm256_setzero_ps();
                let mut vaz = _mm256_setzero_ps();

                let mut j = 0;
                while j < N {
                    let xj = _mm256_loadu_ps(pos_x.as_ptr().add(j));
                    let yj = _mm256_loadu_ps(pos_y.as_ptr().add(j));
                    let zj = _mm256_loadu_ps(pos_z.as_ptr().add(j));
                    let mj = _mm256_loadu_ps(mass.as_ptr().add(j));

                    let dx = _mm256_sub_ps(xj, vpx);
                    let dy = _mm256_sub_ps(yj, vpy);
                    let dz = _mm256_sub_ps(zj, vpz);

                    let d2_0 = _mm256_fmadd_ps(dx, dx, soft_vec);
                    let d2_1 = _mm256_fmadd_ps(dy, dy, d2_0);
                    let dist_sqr = _mm256_fmadd_ps(dz, dz, d2_1);

                    // Reciprocal sqrt with one step Newton-Raphson refinement
                    let y0 = _mm256_rsqrt_ps(dist_sqr);
                    let y0_sqr = _mm256_mul_ps(y0, y0);
                    let half_d2 = _mm256_mul_ps(c_half, dist_sqr);
                    let nr = _mm256_fnmadd_ps(half_d2, y0_sqr, c_one_point_five);
                    let inv_dist = _mm256_mul_ps(y0, nr);

                    let inv_dist2 = _mm256_mul_ps(inv_dist, inv_dist);
                    let inv_dist3 = _mm256_mul_ps(inv_dist, inv_dist2);
                    let s = _mm256_mul_ps(mj, inv_dist3);

                    vax = _mm256_fmadd_ps(dx, s, vax);
                    vay = _mm256_fmadd_ps(dy, s, vay);
                    vaz = _mm256_fmadd_ps(dz, s, vaz);

                    j += 8;
                }

                // Horizontal sum
                let ax = hsum256_ps(vax);
                let ay = hsum256_ps(vay);
                let az = hsum256_ps(vaz);

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

    let checksum = (total_x as i64) + (total_y as i64) + (total_z as i64) + (total_v as i64) + (total_ek as i64);

    println!("TIME_MS:{:.4};TOTAL_X:{};TOTAL_Y:{};TOTAL_Z:{};TOTAL_V:{};TOTAL_EK:{};CHECKSUM:{}",
             elapsed_ms, total_x as i64, total_y as i64, total_z as i64, total_v as i64, total_ek as i64, checksum);
}

#[inline(always)]
unsafe fn hsum256_ps(v: __m256) -> f32 {
    let vlow = _mm256_castps256_ps128(v);
    let vhigh = _mm256_extractf128_ps(v, 1);
    let v128 = _mm_add_ps(vlow, vhigh);
    let shuf = _mm_movehl_ps(v128, v128);
    let sums = _mm_add_ps(v128, shuf);
    let shuf2 = _mm_shuffle_ps(sums, sums, 1);
    let final_sum = _mm_add_ss(sums, shuf2);
    _mm_cvtss_f32(final_sum)
}
