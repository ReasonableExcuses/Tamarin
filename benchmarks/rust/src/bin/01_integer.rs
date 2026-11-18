// 01_integer.rs - Integer Arithmetic Benchmark
// Tests prime sieve, Collatz sequences, and 64-bit unsigned modular hashing

fn collatz_steps(mut n: u64) -> i32 {
    let mut steps = 0;
    while n > 1 {
        if n % 2 == 0 {
            n = n / 2;
        } else {
            n = 3 * n + 1;
        }
        steps += 1;
    }
    steps
}

fn main() {
    let mut is_prime = [0i32; 20000];
    let timer = std::time::Instant::now();

    // 1. Sieve of Eratosthenes (20,000 elements x 50 iterations)
    let mut prime_count = 0;
    for iter in 0..50 {
        for i in 0..20000 {
            is_prime[i] = 1;
        }
        is_prime[0] = 0;
        is_prime[1] = 0;
        let mut p = 2;
        while p * p < 20000 {
            if is_prime[p] == 1 {
                let mut m = p * p;
                while m < 20000 {
                    is_prime[m] = 0;
                    m += p;
                }
            }
            p += 1;
        }
        if iter == 49 {
            for i in 0..20000 {
                if is_prime[i] == 1 {
                    prime_count += 1;
                }
            }
        }
    }

    // 2. Collatz sequence steps (1 to 20,000)
    let mut total_collatz: u64 = 0;
    for i in 1..=20000 {
        total_collatz += collatz_steps(i as u64) as u64;
    }

    // 3. 64-bit unsigned modular integer hashing (2,000,000 iterations)
    let mut h: u64 = 1469598103934665603;
    for i in 0..2000000u64 {
        h = (h.wrapping_mul(1099511628211).wrapping_add(i)) ^ (h >> 17);
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    let checksum: u64 = (prime_count as u64) + total_collatz + (h & 2147483647);
    println!("TIME_MS:{:.4};PRIMES:{};COLLATZ:{};H:{};CHECKSUM:{}",
             elapsed_ms, prime_count, total_collatz, h & 2147483647, checksum);
}
