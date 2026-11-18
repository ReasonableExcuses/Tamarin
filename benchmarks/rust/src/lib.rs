// lib.rs - Sound Region Arena Allocator for Rust Benchmarks

use std::alloc::{alloc, dealloc, Layout};
use std::cell::UnsafeCell;
use std::mem::{align_of, size_of, MaybeUninit};

#[derive(Debug, PartialEq, Eq)]
pub enum ArenaError {
    InvalidAlignment,
    AllocationFailed,
    ArithmeticOverflow,
}

impl std::fmt::Display for ArenaError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ArenaError::InvalidAlignment => write!(f, "Alignment must be a power of two and >= align_of::<T>()"),
            ArenaError::AllocationFailed => write!(f, "Memory allocation failed"),
            ArenaError::ArithmeticOverflow => write!(f, "Arithmetic overflow in size calculation"),
        }
    }
}

impl std::error::Error for ArenaError {}

/// Marker trait certifying that all-zero bit patterns are valid representations of `Self`.
///
/// # Safety
/// Implementing this trait guarantees that an array of all-zero bytes of size `size_of::<Self>()`
/// constitutes a valid, sound instance of `Self`.
pub unsafe trait Zeroable: Copy {}

unsafe impl Zeroable for u8 {}
unsafe impl Zeroable for u16 {}
unsafe impl Zeroable for u32 {}
unsafe impl Zeroable for u64 {}
unsafe impl Zeroable for u128 {}
unsafe impl Zeroable for usize {}
unsafe impl Zeroable for i8 {}
unsafe impl Zeroable for i16 {}
unsafe impl Zeroable for i32 {}
unsafe impl Zeroable for i64 {}
unsafe impl Zeroable for i128 {}
unsafe impl Zeroable for isize {}
unsafe impl Zeroable for f32 {}
unsafe impl Zeroable for f64 {}

#[derive(Clone, Copy)]
struct Chunk {
    ptr: *mut u8,
    layout: Layout,
    offset: usize,
}

struct ArenaState {
    chunks: Vec<Chunk>,
    dtors: Vec<Box<dyn FnOnce()>>,
}

pub struct Arena {
    state: UnsafeCell<ArenaState>,
}

impl Arena {
    pub fn new(cap: usize) -> Self {
        let actual_cap = if cap == 0 { 4096 } else { cap.max(64) };
        let layout = Layout::from_size_align(actual_cap, 16).expect("invalid initial layout");
        let ptr = unsafe { alloc(layout) };
        if ptr.is_null() {
            std::alloc::handle_alloc_error(layout);
        }
        Arena {
            state: UnsafeCell::new(ArenaState {
                chunks: vec![Chunk {
                    ptr,
                    layout,
                    offset: 0,
                }],
                dtors: Vec::new(),
            }),
        }
    }

    /// Registers a destructor closure to execute in reverse order upon arena drop.
    pub fn register_dtor<F: FnOnce() + 'static>(&self, f: F) {
        let state = unsafe { &mut *self.state.get() };
        state.dtors.push(Box::new(f));
    }

    /// Allocates an uninitialized slice of type `T`.
    pub fn alloc_uninit<'a, T>(&'a self, count: usize, align: usize) -> Result<&'a mut [MaybeUninit<T>], ArenaError> {
        self.alloc_raw::<T>(count, align)
    }

    /// Allocates a zero-initialized slice of type `T` where `T: Zeroable`.
    /// Guaranteed to be safe because `T: Zeroable` certifies that all-zero bytes are valid.
    pub fn alloc_zeroed<'a, T: Zeroable>(&'a self, count: usize, align: usize) -> Result<&'a mut [T], ArenaError> {
        let uninit = self.alloc_uninit::<T>(count, align)?;
        unsafe {
            let byte_count = count.checked_mul(size_of::<T>()).ok_or(ArenaError::ArithmeticOverflow)?;
            std::ptr::write_bytes(uninit.as_mut_ptr() as *mut u8, 0, byte_count);
            Ok(std::slice::from_raw_parts_mut(uninit.as_mut_ptr() as *mut T, count))
        }
    }

    /// Safe allocator interface for benchmark workloads.
    /// Restricted to `T: Zeroable` to guarantee zero-initialization soundness.
    #[inline]
    pub fn alloc<'a, T: Zeroable>(&'a self, count: usize, align: usize) -> &'a mut [T] {
        self.alloc_zeroed(count, align).expect("arena allocation failed")
    }

    fn alloc_raw<'a, T>(&'a self, count: usize, align: usize) -> Result<&'a mut [MaybeUninit<T>], ArenaError> {
        if !align.is_power_of_two() || align < align_of::<T>() {
            return Err(ArenaError::InvalidAlignment);
        }

        let elem_size = size_of::<T>();
        let size = count.checked_mul(elem_size).ok_or(ArenaError::ArithmeticOverflow)?;
        if size == 0 {
            return Ok(&mut []);
        }

        let state = unsafe { &mut *self.state.get() };
        let cur_chunk = state.chunks.last_mut().ok_or(ArenaError::AllocationFailed)?;

        let cur_ptr = cur_chunk.ptr as usize;
        let cur_addr = cur_ptr.checked_add(cur_chunk.offset).ok_or(ArenaError::ArithmeticOverflow)?;
        let align_mask = align - 1;
        let aligned_addr = (cur_addr.checked_add(align_mask).ok_or(ArenaError::ArithmeticOverflow)?) & !align_mask;
        let pad = aligned_addr - cur_addr;
        let needed_offset = cur_chunk.offset.checked_add(pad).ok_or(ArenaError::ArithmeticOverflow)?
                                            .checked_add(size).ok_or(ArenaError::ArithmeticOverflow)?;

        if needed_offset <= cur_chunk.layout.size() {
            let offset_start = cur_chunk.offset + pad;
            cur_chunk.offset = needed_offset;
            unsafe {
                let slice_ptr = cur_chunk.ptr.add(offset_start) as *mut MaybeUninit<T>;
                Ok(std::slice::from_raw_parts_mut(slice_ptr, count))
            }
        } else {
            let chunk_align = align.max(16);
            let double_cap = cur_chunk.layout.size().checked_mul(2).ok_or(ArenaError::ArithmeticOverflow)?;
            let needed_cap = size.checked_add(chunk_align).ok_or(ArenaError::ArithmeticOverflow)?
                                 .checked_add(64).ok_or(ArenaError::ArithmeticOverflow)?;
            let next_cap = double_cap.max(needed_cap);

            let layout = Layout::from_size_align(next_cap, chunk_align).map_err(|_| ArenaError::InvalidAlignment)?;
            let ptr = unsafe { alloc(layout) };
            if ptr.is_null() {
                return Err(ArenaError::AllocationFailed);
            }

            let base_addr = ptr as usize;
            let aligned_base = (base_addr.checked_add(align_mask).ok_or(ArenaError::ArithmeticOverflow)?) & !align_mask;
            let initial_pad = aligned_base - base_addr;
            let new_offset = initial_pad.checked_add(size).ok_or(ArenaError::ArithmeticOverflow)?;

            state.chunks.push(Chunk {
                ptr,
                layout,
                offset: new_offset,
            });

            unsafe {
                let slice_ptr = ptr.add(initial_pad) as *mut MaybeUninit<T>;
                Ok(std::slice::from_raw_parts_mut(slice_ptr, count))
            }
        }
    }
}

impl Drop for Arena {
    fn drop(&mut self) {
        let state = self.state.get_mut();
        while let Some(dtor) = state.dtors.pop() {
            dtor();
        }
        for chunk in &state.chunks {
            unsafe {
                dealloc(chunk.ptr, chunk.layout);
            }
        }
    }
}

use std::cell::RefCell;

struct PooledChunkStorage {
    slots: [Option<Chunk>; 4],
}

impl Drop for PooledChunkStorage {
    fn drop(&mut self) {
        for opt in &mut self.slots {
            if let Some(c) = opt.take() {
                unsafe {
                    dealloc(c.ptr, c.layout);
                }
            }
        }
    }
}

thread_local! {
    static TLS_POOLED_CHUNK: RefCell<PooledChunkStorage> = RefCell::new(PooledChunkStorage { slots: [None, None, None, None] });
}

struct PooledArenaState {
    first_chunk: Chunk,
    extra_chunks: Option<Vec<Chunk>>,
    dtors: Option<Vec<Box<dyn FnOnce()>>>,
}

pub struct PooledArena {
    state: UnsafeCell<PooledArenaState>,
}

impl PooledArena {
    pub fn new(cap: usize) -> Self {
        let actual_cap = if cap == 0 { 65536 } else { cap.max(64) };
        let pooled = TLS_POOLED_CHUNK.with(|cell| {
            let mut storage = cell.borrow_mut();
            let mut best_idx = None;
            let mut best_size = usize::MAX;
            for (i, opt) in storage.slots.iter().enumerate() {
                if let Some(chunk) = opt {
                    let sz = chunk.layout.size();
                    if sz >= actual_cap && sz < best_size {
                        best_size = sz;
                        best_idx = Some(i);
                        if sz == actual_cap { break; }
                    }
                }
            }
            if let Some(idx) = best_idx {
                return storage.slots[idx].take();
            }
            None
        });

        let first_chunk = match pooled {
            Some(mut c) => {
                c.offset = 0;
                c
            }
            None => {
                let layout = Layout::from_size_align(actual_cap, 16).expect("invalid layout");
                let ptr = unsafe { alloc(layout) };
                if ptr.is_null() {
                    std::alloc::handle_alloc_error(layout);
                }
                Chunk { ptr, layout, offset: 0 }
            }
        };

        PooledArena {
            state: UnsafeCell::new(PooledArenaState {
                first_chunk,
                extra_chunks: None,
                dtors: None,
            }),
        }
    }

    pub fn register_dtor<F: FnOnce() + 'static>(&self, f: F) {
        let state = unsafe { &mut *self.state.get() };
        state.dtors.get_or_insert_with(Vec::new).push(Box::new(f));
    }

    pub fn alloc_uninit<'a, T>(&'a self, count: usize, align: usize) -> Result<&'a mut [MaybeUninit<T>], ArenaError> {
        self.alloc_raw::<T>(count, align)
    }

    pub fn alloc_zeroed<'a, T: Zeroable>(&'a self, count: usize, align: usize) -> Result<&'a mut [T], ArenaError> {
        let uninit = self.alloc_uninit::<T>(count, align)?;
        unsafe {
            let byte_count = count.checked_mul(size_of::<T>()).ok_or(ArenaError::ArithmeticOverflow)?;
            std::ptr::write_bytes(uninit.as_mut_ptr() as *mut u8, 0, byte_count);
            Ok(std::slice::from_raw_parts_mut(uninit.as_mut_ptr() as *mut T, count))
        }
    }

    #[inline]
    pub fn alloc<'a, T: Zeroable>(&'a self, count: usize, align: usize) -> &'a mut [T] {
        self.alloc_zeroed(count, align).expect("arena allocation failed")
    }

    fn alloc_raw<'a, T>(&'a self, count: usize, align: usize) -> Result<&'a mut [MaybeUninit<T>], ArenaError> {
        if !align.is_power_of_two() || align < align_of::<T>() {
            return Err(ArenaError::InvalidAlignment);
        }

        let elem_size = size_of::<T>();
        let size = count.checked_mul(elem_size).ok_or(ArenaError::ArithmeticOverflow)?;
        if size == 0 {
            return Ok(&mut []);
        }

        let state = unsafe { &mut *self.state.get() };
        let cur_chunk = match &mut state.extra_chunks {
            Some(extras) if !extras.is_empty() => extras.last_mut().unwrap(),
            _ => &mut state.first_chunk,
        };

        let cur_ptr = cur_chunk.ptr as usize;
        let cur_addr = cur_ptr.checked_add(cur_chunk.offset).ok_or(ArenaError::ArithmeticOverflow)?;
        let align_mask = align - 1;
        let aligned_addr = (cur_addr.checked_add(align_mask).ok_or(ArenaError::ArithmeticOverflow)?) & !align_mask;
        let pad = aligned_addr - cur_addr;
        let needed_offset = cur_chunk.offset.checked_add(pad).ok_or(ArenaError::ArithmeticOverflow)?
                                            .checked_add(size).ok_or(ArenaError::ArithmeticOverflow)?;

        if needed_offset <= cur_chunk.layout.size() {
            let offset_start = cur_chunk.offset + pad;
            cur_chunk.offset = needed_offset;
            unsafe {
                let slice_ptr = cur_chunk.ptr.add(offset_start) as *mut MaybeUninit<T>;
                Ok(std::slice::from_raw_parts_mut(slice_ptr, count))
            }
        } else {
            let chunk_align = align.max(16);
            let double_cap = cur_chunk.layout.size().checked_mul(2).ok_or(ArenaError::ArithmeticOverflow)?;
            let needed_cap = size.checked_add(chunk_align).ok_or(ArenaError::ArithmeticOverflow)?
                                 .checked_add(64).ok_or(ArenaError::ArithmeticOverflow)?;
            let next_cap = double_cap.max(needed_cap);

            // Check if there is an existing pooled chunk that fits
            let pooled = TLS_POOLED_CHUNK.with(|cell| {
                let mut storage = cell.borrow_mut();
                let mut best_idx = None;
                let mut best_sz = usize::MAX;
                for (i, opt) in storage.slots.iter().enumerate() {
                    if let Some(chunk) = opt {
                        let sz = chunk.layout.size();
                        if sz >= next_cap && chunk.layout.align() >= chunk_align && sz < best_sz {
                            best_sz = sz;
                            best_idx = Some(i);
                        }
                    }
                }
                if let Some(idx) = best_idx {
                    return storage.slots[idx].take();
                }
                None
            });

            let new_chunk = match pooled {
                Some(mut c) => {
                    let base_addr = c.ptr as usize;
                    let aligned_base = (base_addr.checked_add(align_mask).ok_or(ArenaError::ArithmeticOverflow)?) & !align_mask;
                    let initial_pad = aligned_base - base_addr;
                    let new_offset = initial_pad.checked_add(size).ok_or(ArenaError::ArithmeticOverflow)?;
                    c.offset = new_offset;
                    c
                }
                None => {
                    let layout = Layout::from_size_align(next_cap, chunk_align).map_err(|_| ArenaError::InvalidAlignment)?;
                    let ptr = unsafe { alloc(layout) };
                    if ptr.is_null() {
                        return Err(ArenaError::AllocationFailed);
                    }
                    let base_addr = ptr as usize;
                    let aligned_base = (base_addr.checked_add(align_mask).ok_or(ArenaError::ArithmeticOverflow)?) & !align_mask;
                    let initial_pad = aligned_base - base_addr;
                    let new_offset = initial_pad.checked_add(size).ok_or(ArenaError::ArithmeticOverflow)?;
                    Chunk {
                        ptr,
                        layout,
                        offset: new_offset,
                    }
                }
            };

            let ptr = new_chunk.ptr;
            let base_addr = ptr as usize;
            let aligned_base = (base_addr + align_mask) & !align_mask;
            let initial_pad = aligned_base - base_addr;

            let extras = state.extra_chunks.get_or_insert_with(Vec::new);
            extras.push(new_chunk);

            unsafe {
                let slice_ptr = ptr.add(initial_pad) as *mut MaybeUninit<T>;
                Ok(std::slice::from_raw_parts_mut(slice_ptr, count))
            }
        }
    }
}

impl Drop for PooledArena {
    fn drop(&mut self) {
        let state = self.state.get_mut();
        if let Some(dtors) = &mut state.dtors {
            while let Some(dtor) = dtors.pop() {
                dtor();
            }
        }

        if let Some(mut extras) = state.extra_chunks.take() {
            for mut chunk in extras.drain(..) {
                chunk.offset = 0;
                TLS_POOLED_CHUNK.with(|cell| {
                    let mut storage = cell.borrow_mut();
                    let mut cached = false;
                    for opt in &mut storage.slots {
                        if opt.is_none() && chunk.layout.size() <= 262144 {
                            *opt = Some(chunk);
                            cached = true;
                            break;
                        }
                    }
                    if !cached {
                        unsafe { dealloc(chunk.ptr, chunk.layout); }
                    }
                });
            }
        }

        let mut first = state.first_chunk;
        first.offset = 0;
        TLS_POOLED_CHUNK.with(|cell| {
            let mut storage = cell.borrow_mut();
            let mut cached = false;
            for opt in &mut storage.slots {
                if opt.is_none() && first.layout.size() <= 262144 {
                    *opt = Some(first);
                    cached = true;
                    break;
                }
            }
            if !cached {
                unsafe { dealloc(first.ptr, first.layout); }
            }
        });
    }
}
