// ============================================================================
// page_provider.h - Operating System Virtual Memory Page Provider Abstraction
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace tamarin::runtime {

/// Abstract provider interface for virtual memory address reservation,
/// page commitment, decommitment, and whole-reservation release.
struct MemoryPageProvider {
    virtual ~MemoryPageProvider() = default;

    /// Reserve a range of virtual address space without committing physical storage.
    virtual void* reserve(size_t size) = 0;

    /// Commit physical storage for a previously reserved range [offset, offset + size).
    virtual bool commit(void* base, size_t offset, size_t size) = 0;

    /// Decommit physical storage from a range while retaining the address reservation.
    virtual bool decommit(void* base, size_t offset, size_t size) = 0;

    /// Release an entire virtual address space reservation back to the OS.
    virtual bool release(void* base) = 0;

    /// System virtual memory page size in bytes (e.g., 4096).
    virtual size_t pageSize() const = 0;

    /// System virtual memory allocation granularity in bytes (e.g., 65536 on Windows x64).
    virtual size_t allocationGranularity() const = 0;

    /// Process resident working set memory in bytes (0 if unsupported / not measured).
    virtual size_t getResidentWorkingSet() const { return 0; }

    /// OS virtual memory operation counters
    virtual uint64_t getReserveCount() const { return 0; }
    virtual uint64_t getCommitCount() const { return 0; }
    virtual uint64_t getDecommitCount() const { return 0; }
    virtual uint64_t getReleaseCount() const { return 0; }
    virtual void resetCounters() {}
};

#ifdef _WIN32
/// Windows implementation of MemoryPageProvider using VirtualAlloc / VirtualFree.
class WindowsPageProvider final : public MemoryPageProvider {
public:
    WindowsPageProvider();
    WindowsPageProvider(size_t customPageSize, size_t customAllocGran);

    void* reserve(size_t size) override;
    bool commit(void* base, size_t offset, size_t size) override;
    bool decommit(void* base, size_t offset, size_t size) override;
    bool release(void* base) override;

    size_t pageSize() const override { return pageSize_; }
    size_t allocationGranularity() const override { return allocGranularity_; }
    size_t getResidentWorkingSet() const override;

    void setPageSize(size_t ps) { pageSize_ = ps; }
    void setAllocationGranularity(size_t ag) { allocGranularity_ = ag; }

private:
    size_t pageSize_{4096};
    size_t allocGranularity_{65536};
};
#else
/// POSIX implementation of MemoryPageProvider using mmap / mprotect / madvise / munmap.
class PosixPageProvider final : public MemoryPageProvider {
public:
    PosixPageProvider();
    PosixPageProvider(size_t customPageSize, size_t customAllocGran);

    void* reserve(size_t size) override;
    bool commit(void* base, size_t offset, size_t size) override;
    bool decommit(void* base, size_t offset, size_t size) override;
    bool release(void* base) override;

    size_t pageSize() const override { return pageSize_; }
    size_t allocationGranularity() const override { return allocGranularity_; }
    size_t getResidentWorkingSet() const override;

    void setPageSize(size_t ps) { pageSize_ = ps; }
    void setAllocationGranularity(size_t ag) { allocGranularity_ = ag; }

private:
    size_t pageSize_{4096};
    size_t allocGranularity_{65536};
};
#endif

/// Portable checked fallback page provider using standard aligned heap facilities.
class FallbackPageProvider final : public MemoryPageProvider {
public:
    FallbackPageProvider() = default;
    FallbackPageProvider(size_t customPageSize, size_t customAllocGran)
        : pageSize_(customPageSize), allocGranularity_(customAllocGran) {}

    void* reserve(size_t size) override;
    bool commit(void* base, size_t offset, size_t size) override;
    bool decommit(void* base, size_t offset, size_t size) override;
    bool release(void* base) override;

    size_t pageSize() const override { return pageSize_; }
    size_t allocationGranularity() const override { return allocGranularity_; }
    size_t getResidentWorkingSet() const override { return 0; }

    void setPageSize(size_t ps) { pageSize_ = ps; }
    void setAllocationGranularity(size_t ag) { allocGranularity_ = ag; }

private:
    size_t pageSize_{4096};
    size_t allocGranularity_{65536};
};

/// Get the platform's active memory page provider instance.
MemoryPageProvider& getPlatformPageProvider();

/// Set a custom active memory page provider (pass nullptr to reset to default).
void setPlatformPageProvider(MemoryPageProvider* provider);

/// Reset platform page provider to default.
void resetPlatformPageProvider();

// ── Generational Slab Runtime Structures and ABI ────────────────────────────
struct TamarinSlabChunk {
    uint8_t* slots;              // Chunk slots memory
    uint32_t* generations;       // Chunk generation array
    uint32_t capacity;           // Slots capacity in this chunk
    uint32_t count;              // Active slots in this chunk
    uint32_t free_head;          // Index of first free slot in this chunk (0xFFFFFFFF = empty)
    uint32_t slot_offset;        // Global slot offset for this chunk
    TamarinSlabChunk* next;      // Next linked chunk
    void* memory_base;           // Backing virtual allocation base
    size_t memory_reserved;      // Reserved bytes
    size_t memory_committed;     // Committed bytes
};

struct TamarinSlab {
    uint8_t* slots;              // Primary chunk slots
    uint32_t* generations;       // Primary chunk generations
    uint64_t capacity;           // Total capacity in slots
    uint64_t count;              // Total active slots
    uint64_t free_head;          // Global free-list head (0xFFFFFFFF = empty)
    uint64_t elem_size;          // Logical element size in bytes
    uint64_t slot_stride;        // Slot stride in bytes (max(elem_size, 4), aligned)
    TamarinSlabChunk* chunks;    // Secondary chunks linked list
    void* memory_base;           // Primary chunk virtual allocation base
    size_t memory_reserved;      // Primary chunk reserved bytes
    size_t memory_committed;     // Primary chunk committed bytes
};

} // namespace tamarin::runtime

using TamarinSlabChunk = tamarin::runtime::TamarinSlabChunk;
using TamarinSlab = tamarin::runtime::TamarinSlab;

// ── C-linkage runtime exports for generated TAMARIN programs ─────────────────
extern "C" {
void* __tamarin_runtime_page_reserve(size_t size);
void* __tamarin_runtime_page_commit(void* base, size_t offset, size_t size);
int32_t __tamarin_runtime_page_decommit(void* base, size_t offset, size_t size);
int32_t __tamarin_runtime_page_release(void* base);
size_t __tamarin_runtime_page_size();
size_t __tamarin_runtime_allocation_granularity();
size_t __tamarin_runtime_working_set_bytes();

// Thread-local reservation cache and fast small-region pool API
void* __tamarin_runtime_rcache_acquire(size_t needed_res, size_t* out_reserved = nullptr, size_t* out_committed = nullptr);
int32_t __tamarin_runtime_rcache_put(void* base, size_t reserved_size, size_t committed_size, size_t used_size = 0);
void __tamarin_runtime_rcache_clear();
void __tamarin_runtime_rcache_set_policy(int32_t policy, size_t memory_cap);
int32_t __tamarin_runtime_rcache_get_policy();
size_t __tamarin_runtime_rcache_get_aggregate_committed();

// Thread-local large-chunk and growth-chunk reuse pool API
void* __tamarin_runtime_chunk_cache_acquire(size_t needed_res, size_t* out_reserved = nullptr, size_t* out_committed = nullptr);
int32_t __tamarin_runtime_chunk_cache_put(void* base, size_t reserved_size, size_t committed_size, size_t used_size = 0);
void __tamarin_runtime_chunk_cache_clear();
void __tamarin_runtime_chunk_cache_set_policy(int32_t policy, size_t memory_cap);
int32_t __tamarin_runtime_chunk_cache_get_policy();
size_t __tamarin_runtime_chunk_cache_get_aggregate_committed();

// Thread-local sidecar chunk reuse pool API
void* __tamarin_runtime_sidecar_cache_acquire();
void __tamarin_runtime_sidecar_cache_put(void* chunk);
void __tamarin_runtime_sidecar_cache_clear();

// OS Virtual Memory counters
uint64_t __tamarin_runtime_get_reserve_count();
uint64_t __tamarin_runtime_get_commit_count();
uint64_t __tamarin_runtime_get_decommit_count();
uint64_t __tamarin_runtime_get_release_count();
void __tamarin_runtime_reset_counters();

// Provider hook for testing
void __tamarin_runtime_set_page_provider(tamarin::runtime::MemoryPageProvider* provider);
tamarin::runtime::MemoryPageProvider* __tamarin_runtime_get_page_provider();

// Generational Slab API
void __tamarin_slab_init(TamarinSlab* slab, size_t elem_size, size_t elem_align, size_t initial_cap);
uint64_t __tamarin_slab_insert(TamarinSlab* slab, const void* elem_val);
void* __tamarin_slab_get(TamarinSlab* slab, uint64_t handle);
int32_t __tamarin_slab_is_valid(const TamarinSlab* slab, uint64_t handle);
int32_t __tamarin_slab_try_get(const TamarinSlab* slab, uint64_t handle, void* out_val);
int32_t __tamarin_slab_remove(TamarinSlab* slab, uint64_t handle, void (*dtor)(void*));
uint64_t __tamarin_slab_count(const TamarinSlab* slab);
uint64_t __tamarin_slab_capacity(const TamarinSlab* slab);
void __tamarin_slab_clear(TamarinSlab* slab, void (*dtor)(void*));
void __tamarin_slab_destroy(TamarinSlab* slab, void (*dtor)(void*));
void __tamarin_slab_panic(uint32_t slot, uint32_t handle_gen, uint32_t actual_gen);
}
