// 12_adt_matching_expert.rs - Milestone v0.7: Expert Algebraic Data Types, Pattern Matching & Error Handling
use std::num::NonZeroU64;

#[derive(Clone, Copy)]
enum Event {
    Quit,
    Move(i32, i32),
    Damage(i32, i32),
    Telemetry(i32, i32, i32),
}

#[derive(Clone, Copy)]
struct Entity {
    id: i32,
}

#[inline(always)]
fn stage1(val: i32) -> Result<i32, i32> {
    if (val & 7) == 0 {
        return Err(1);
    }
    Ok(val + 10)
}

#[inline(always)]
fn stage2(val: i32) -> Result<i32, i32> {
    if (val & 32) != 0 {
        return Err(2);
    }
    Ok(val * 3)
}

#[inline(always)]
fn stage3(val: i32) -> Result<i32, i32> {
    if (val & 256) != 0 {
        return Err(3);
    }
    Ok(val ^ 23130)
}

#[inline(always)]
fn run_pipeline(val: i32) -> Result<i32, i32> {
    let v1 = stage1(val)?;
    let v2 = stage2(v1)?;
    let v3 = stage3(v2)?;
    Ok(v3)
}

fn main() {
    let start = std::time::Instant::now();

    let mut lcg: i64 = 2147483647;

    // ── Part 1: ADT Pattern Matching (2,000,000 iterations) ──
    let mut x: i64 = 0;
    let mut y: i64 = 0;
    let mut damage_sum: i64 = 0;
    let mut telemetry_sum: i64 = 0;
    let mut quit_count: i64 = 0;

    for _ in 0..2000000 {
        lcg = (lcg.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        let ev_tag = (lcg % 4) as i32;

        let ev = if ev_tag == 0 {
            Event::Quit
        } else if ev_tag == 1 {
            let dx = ((lcg >> 16) & 255) as i32;
            let dy = ((lcg >> 8) & 255) as i32;
            Event::Move(dx, dy)
        } else if ev_tag == 2 {
            let target_id = ((lcg >> 16) & 1023) as i32;
            let amount = (lcg & 255) as i32;
            Event::Damage(target_id, amount)
        } else {
            let code = ((lcg >> 16) & 255) as i32;
            let length = ((lcg >> 8) & 127) as i32;
            let flags = (lcg & 31) as i32;
            Event::Telemetry(code, length, flags)
        };

        match ev {
            Event::Quit => {
                quit_count += 1;
            }
            Event::Move(dx, dy) => {
                x += dx as i64;
                y += dy as i64;
            }
            Event::Damage(target_id, amount) => {
                damage_sum += (target_id as i64) * (amount as i64);
            }
            Event::Telemetry(code, length, flags) => {
                telemetry_sum += ((code ^ length) + flags) as i64;
            }
        }
    }

    let adt_sum: i64 = x + y + damage_sum + telemetry_sum + quit_count;

    // ── Part 2: Safe Error Handling with Result and ? (1,000,000 iterations) ──
    let mut ok_sum: i64 = 0;
    let mut err_sum: i64 = 0;

    for _ in 0..1000000 {
        lcg = (lcg.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        let val = (lcg & 65535) as i32;

        match run_pipeline(val) {
            Ok(r) => {
                ok_sum += r as i64;
            }
            Err(e) => {
                err_sum += e as i64;
            }
        }
    }

    let try_sum: i64 = ok_sum - err_sum;

    // ── Part 3: Niche-Optimized Option Handle (500,000 iterations) ──
    let mut entities: [Entity; 1024] = [Entity { id: 0 }; 1024];
    let mut handles: [NonZeroU64; 1024] = [NonZeroU64::new(1).unwrap(); 1024];
    for k in 0..1024 {
        entities[k] = Entity { id: (k + 1) as i32 };
        handles[k] = NonZeroU64::new((1u64 << 32) | (k as u64)).unwrap();
    }

    let mut niche_sum: i64 = 0;
    for _ in 0..500000 {
        lcg = (lcg.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        let mut opt: Option<NonZeroU64> = None;
        if (lcg & 3) != 0 {
            let idx = (lcg & 1023) as usize;
            opt = Some(unsafe { *handles.get_unchecked(idx) });
        }

        match opt {
            Some(h) => {
                let slot = (h.get() & 1023) as usize;
                niche_sum += unsafe { entities.get_unchecked(slot).id } as i64;
            }
            None => {
                niche_sum -= 42;
            }
        }
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

    let checksum: i64 = adt_sum + try_sum + niche_sum;

    println!("TIME_MS:{:.4};ADT_SUM:{};TRY_SUM:{};NICHE_SUM:{};CHECKSUM:{}",
             elapsed_ms, adt_sum, try_sum, niche_sum, checksum);
}
