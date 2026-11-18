// soundness_tests.rs - Validation tests for Rust Arena soundness

use tamarin_benchmarks_rust::{Arena, ArenaError, PooledArena};

#[test]
fn test_invalid_alignment_non_power_of_two() {
    let arena = Arena::new(4096);
    // Alignment 3 is not a power of two
    let res = arena.alloc_uninit::<u32>(10, 3);
    assert_eq!(res.err(), Some(ArenaError::InvalidAlignment));

    // Alignment 0 is not a power of two
    let res0 = arena.alloc_uninit::<u32>(10, 0);
    assert_eq!(res0.err(), Some(ArenaError::InvalidAlignment));

    // Alignment 7 is not a power of two
    let res7 = arena.alloc_uninit::<u64>(10, 7);
    assert_eq!(res7.err(), Some(ArenaError::InvalidAlignment));
}

#[test]
fn test_invalid_alignment_less_than_type_align() {
    let arena = Arena::new(4096);
    // u64 requires at least align 8, passing align 4 or 2 must fail
    let res = arena.alloc_uninit::<u64>(10, 4);
    assert_eq!(res.err(), Some(ArenaError::InvalidAlignment));

    let res2 = arena.alloc_uninit::<u64>(10, 2);
    assert_eq!(res2.err(), Some(ArenaError::InvalidAlignment));

    let res1 = arena.alloc_uninit::<u64>(10, 1);
    assert_eq!(res1.err(), Some(ArenaError::InvalidAlignment));
}

#[test]
fn test_valid_alignment_and_zeroing() {
    let arena = Arena::new(4096);
    // 16-byte alignment on u64 array
    let slice = arena.alloc_zeroed::<u64>(32, 16).expect("should succeed");
    assert_eq!(slice.len(), 32);
    let addr = slice.as_ptr() as usize;
    assert_eq!(addr % 16, 0, "address must be 16-byte aligned");
    for &val in slice.iter() {
        assert_eq!(val, 0, "must be zero-initialized");
    }
}

#[test]
fn test_arithmetic_overflow_protection() {
    let arena = Arena::new(4096);
    // Count that overflows usize::MAX when multiplied by size_of::<u64>()
    let huge_count = usize::MAX / 2;
    let res = arena.alloc_uninit::<u64>(huge_count, 8);
    assert_eq!(res.err(), Some(ArenaError::ArithmeticOverflow));
}

#[test]
fn test_chunk_growth_maintains_alignment() {
    let arena = Arena::new(128); // very small initial chunk
    // Allocate 1024 u64 elements requiring growth with 64-byte alignment
    let slice = arena.alloc_zeroed::<u64>(1024, 64).expect("growth should succeed");
    assert_eq!(slice.len(), 1024);
    let addr = slice.as_ptr() as usize;
    assert_eq!(addr % 64, 0, "grown chunk address must satisfy requested 64-byte alignment");
}

#[test]
fn test_destructor_order_lifo() {
    use std::sync::Arc;

    let log = Arc::new(std::sync::Mutex::new(Vec::new()));
    {
        let arena = Arena::new(4096);
        let l1 = log.clone();
        arena.register_dtor(move || {
            l1.lock().unwrap().push(1);
        });
        let l2 = log.clone();
        arena.register_dtor(move || {
            l2.lock().unwrap().push(2);
        });
        let l3 = log.clone();
        arena.register_dtor(move || {
            l3.lock().unwrap().push(3);
        });
    } // arena drops here

    let history = log.lock().unwrap().clone();
    assert_eq!(history, vec![3, 2, 1], "Destructors must execute in strict LIFO order");
}

// A non-zeroable type without Zeroable trait
#[derive(Clone, Copy)]
struct NonZeroType(&'static str);

#[test]
fn test_non_zeroable_types_use_uninit() {
    let arena = Arena::new(4096);
    // NonZeroType does not implement Zeroable, so alloc_zeroed cannot be called on it!
    // But alloc_uninit can be safely called:
    let uninit_slice = arena.alloc_uninit::<NonZeroType>(4, std::mem::align_of::<NonZeroType>())
                            .expect("alloc_uninit should succeed");
    assert_eq!(uninit_slice.len(), 4);
    // Initialize elements properly before reading
    for elem in uninit_slice.iter_mut() {
        elem.write(NonZeroType("hello"));
    }
    unsafe {
        let init_slice = std::slice::from_raw_parts(uninit_slice.as_ptr() as *const NonZeroType, 4);
        assert_eq!(init_slice[0].0, "hello");
    }
}

#[test]
fn test_pooled_arena_reuse_and_growth() {
    let mut initial_ptr: *const u64 = std::ptr::null();
    for iter in 0..10 {
        let arena = PooledArena::new(65536);
        let slice = arena.alloc::<u64>(100, 8);
        assert_eq!(slice.len(), 100);
        if iter == 0 {
            initial_ptr = slice.as_ptr();
        } else {
            // Buffer must be reused across iterations in the steady state
            assert_eq!(slice.as_ptr(), initial_ptr, "PooledArena must reuse cached chunk");
        }
        for (i, val) in slice.iter_mut().enumerate() {
            *val = (iter * 100 + i) as u64;
        }
    }
}

#[test]
fn test_pooled_arena_thread_exit_cleanup() {
    let handle = std::thread::spawn(|| {
        let arena = PooledArena::new(65536);
        let slice = arena.alloc::<u64>(50, 8);
        assert_eq!(slice.len(), 50);
        slice[0] = 42;
    });
    handle.join().expect("thread join must succeed without leak or panic");
}
