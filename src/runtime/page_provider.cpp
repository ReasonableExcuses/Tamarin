// ============================================================================
// page_provider.cpp - Implementation of OS Virtual Memory Page Providers
// ============================================================================

#include "tamarin/runtime/page_provider.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <algorithm>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace tamarin::runtime {

struct PageProviderStats {
    std::atomic<uint64_t> reserve_count{0};
    std::atomic<uint64_t> commit_count{0};
    std::atomic<uint64_t> decommit_count{0};
    std::atomic<uint64_t> release_count{0};
};
static PageProviderStats s_stats;

#ifdef _WIN32

WindowsPageProvider::WindowsPageProvider() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    pageSize_ = sysInfo.dwPageSize > 0 ? static_cast<size_t>(sysInfo.dwPageSize) : 4096;
    allocGranularity_ = sysInfo.dwAllocationGranularity > 0 ? static_cast<size_t>(sysInfo.dwAllocationGranularity) : 65536;

    const char* envPs = std::getenv("TAMARIN_PAGE_SIZE");
    if (envPs && *envPs) {
        size_t val = static_cast<size_t>(std::strtoull(envPs, nullptr, 10));
        if (val > 0) pageSize_ = val;
    }
    const char* envAg = std::getenv("TAMARIN_ALLOC_GRANULARITY");
    if (envAg && *envAg) {
        size_t val = static_cast<size_t>(std::strtoull(envAg, nullptr, 10));
        if (val > 0) allocGranularity_ = val;
    }
}

WindowsPageProvider::WindowsPageProvider(size_t customPageSize, size_t customAllocGran)
    : pageSize_(customPageSize), allocGranularity_(customAllocGran) {}

void* WindowsPageProvider::reserve(size_t size) {
    if (size == 0) return nullptr;
    s_stats.reserve_count.fetch_add(1, std::memory_order_relaxed);
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
}

bool WindowsPageProvider::commit(void* base, size_t offset, size_t size) {
    if (!base || size == 0) return false;
    char* target = static_cast<char*>(base) + offset;
    s_stats.commit_count.fetch_add(1, std::memory_order_relaxed);
    void* result = VirtualAlloc(target, size, MEM_COMMIT, PAGE_READWRITE);
    return result != nullptr;
}

bool WindowsPageProvider::decommit(void* base, size_t offset, size_t size) {
    if (!base || size == 0) return false;
    char* target = static_cast<char*>(base) + offset;
    s_stats.decommit_count.fetch_add(1, std::memory_order_relaxed);
    return VirtualFree(target, size, MEM_DECOMMIT) != 0;
}

bool WindowsPageProvider::release(void* base) {
    if (!base) return false;
    s_stats.release_count.fetch_add(1, std::memory_order_relaxed);
    return VirtualFree(base, 0, MEM_RELEASE) != 0;
}

size_t WindowsPageProvider::getResidentWorkingSet() const {
    PROCESS_MEMORY_COUNTERS pmc;
    std::memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<size_t>(pmc.WorkingSetSize);
    }
    return 0;
}

#endif

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#include <mutex>
#include <unordered_map>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

static std::mutex s_posixReservationMtx;
static std::unordered_map<void*, size_t> s_posixReservations;

PosixPageProvider::PosixPageProvider() {
    long sz = sysconf(_SC_PAGESIZE);
    size_t osPage = sz > 0 ? static_cast<size_t>(sz) : 4096;
    pageSize_ = std::max<size_t>(4096, osPage);
    allocGranularity_ = 65536; // 64 KB reservation granularity across all platforms

    const char* envPs = std::getenv("TAMARIN_PAGE_SIZE");
    if (envPs && *envPs) {
        size_t val = static_cast<size_t>(std::strtoull(envPs, nullptr, 10));
        if (val > 0) pageSize_ = val;
    }
    const char* envAg = std::getenv("TAMARIN_ALLOC_GRANULARITY");
    if (envAg && *envAg) {
        size_t val = static_cast<size_t>(std::strtoull(envAg, nullptr, 10));
        if (val > 0) allocGranularity_ = val;
    }
}

PosixPageProvider::PosixPageProvider(size_t customPageSize, size_t customAllocGran)
    : pageSize_(customPageSize), allocGranularity_(customAllocGran) {}

void* PosixPageProvider::reserve(size_t size) {
    if (size == 0) return nullptr;
    size_t alignedSize = (size + allocGranularity_ - 1) & ~(allocGranularity_ - 1);
    s_stats.reserve_count.fetch_add(1, std::memory_order_relaxed);
    void* ptr = mmap(nullptr, alignedSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;
    {
        std::lock_guard<std::mutex> lock(s_posixReservationMtx);
        s_posixReservations[ptr] = alignedSize;
    }
    return ptr;
}

bool PosixPageProvider::commit(void* base, size_t offset, size_t size) {
    if (!base || size == 0) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(base) + offset;
    uintptr_t alignedStart = addr & ~(pageSize_ - 1);
    uintptr_t alignedEnd = (addr + size + pageSize_ - 1) & ~(pageSize_ - 1);
    size_t alignedLen = alignedEnd - alignedStart;
    s_stats.commit_count.fetch_add(1, std::memory_order_relaxed);
    return mprotect(reinterpret_cast<void*>(alignedStart), alignedLen, PROT_READ | PROT_WRITE) == 0;
}

bool PosixPageProvider::decommit(void* base, size_t offset, size_t size) {
    if (!base || size == 0) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(base) + offset;
    uintptr_t alignedStart = addr & ~(pageSize_ - 1);
    uintptr_t alignedEnd = (addr + size + pageSize_ - 1) & ~(pageSize_ - 1);
    size_t alignedLen = alignedEnd - alignedStart;
    s_stats.decommit_count.fetch_add(1, std::memory_order_relaxed);
#ifdef MADV_DONTNEED
    madvise(reinterpret_cast<void*>(alignedStart), alignedLen, MADV_DONTNEED);
#endif
    return mprotect(reinterpret_cast<void*>(alignedStart), alignedLen, PROT_NONE) == 0;
}

bool PosixPageProvider::release(void* base) {
    if (!base) return false;
    size_t size = 0;
    {
        std::lock_guard<std::mutex> lock(s_posixReservationMtx);
        auto it = s_posixReservations.find(base);
        if (it != s_posixReservations.end()) {
            size = it->second;
            s_posixReservations.erase(it);
        }
    }
    if (size == 0) return false;
    s_stats.release_count.fetch_add(1, std::memory_order_relaxed);
    return munmap(base, size) == 0;
}

size_t PosixPageProvider::getResidentWorkingSet() const {
#if defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<size_t>(info.resident_size);
    }
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return static_cast<size_t>(usage.ru_maxrss) * 1024;
    }
#endif
    return 0;
}
#endif

// ── Fallback Page Provider ──────────────────────────────────────────────────

void* FallbackPageProvider::reserve(size_t size) {
    if (size == 0) return nullptr;
    s_stats.reserve_count.fetch_add(1, std::memory_order_relaxed);
    return std::calloc(1, size);
}

bool FallbackPageProvider::commit(void* base, size_t offset, size_t size) {
    if (!base || size == 0) return false;
    s_stats.commit_count.fetch_add(1, std::memory_order_relaxed);
    std::memset(static_cast<char*>(base) + offset, 0, size);
    return true;
}

bool FallbackPageProvider::decommit(void* base, size_t offset, size_t size) {
    (void)offset;
    (void)size;
    if (!base || size == 0) return false;
    s_stats.decommit_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool FallbackPageProvider::release(void* base) {
    if (!base) return false;
    s_stats.release_count.fetch_add(1, std::memory_order_relaxed);
    std::free(base);
    return true;
}

// ── Platform Provider Registry ──────────────────────────────────────────────

static std::unique_ptr<MemoryPageProvider> s_defaultProvider;
static MemoryPageProvider* s_activeProvider = nullptr;

static void ensureDefaultProvider() {
    if (!s_defaultProvider) {
        const char* envFallback = std::getenv("TAMARIN_USE_FALLBACK_PROVIDER");
        if (envFallback && (std::strcmp(envFallback, "1") == 0 || std::strcmp(envFallback, "true") == 0)) {
            size_t ps = 4096;
            size_t ag = 65536;
            const char* envPs = std::getenv("TAMARIN_PAGE_SIZE");
            if (envPs && *envPs) ps = static_cast<size_t>(std::strtoull(envPs, nullptr, 10));
            const char* envAg = std::getenv("TAMARIN_ALLOC_GRANULARITY");
            if (envAg && *envAg) ag = static_cast<size_t>(std::strtoull(envAg, nullptr, 10));
            s_defaultProvider = std::make_unique<FallbackPageProvider>(ps, ag);
        } else {
#ifdef _WIN32
            s_defaultProvider = std::make_unique<WindowsPageProvider>();
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
            s_defaultProvider = std::make_unique<PosixPageProvider>();
#else
            s_defaultProvider = std::make_unique<FallbackPageProvider>();
#endif
        }
    }
}

MemoryPageProvider& getPlatformPageProvider() {
    if (s_activeProvider) return *s_activeProvider;
    ensureDefaultProvider();
    return *s_defaultProvider;
}

void setPlatformPageProvider(MemoryPageProvider* provider) {
    s_activeProvider = provider;
}

void resetPlatformPageProvider() {
    s_activeProvider = nullptr;
}

// ── Thread-Local Reservation Cache & Fast Small-Region Pool ───────────────────

struct ThreadReservationCache {
    struct Slot {
        void* base = nullptr;
        size_t reserved_size = 0;
        size_t committed_size = 0;
    };
    static constexpr size_t MAX_SLOTS = 4;
    Slot slots[MAX_SLOTS];
    int32_t policy = 1; // 0 = decommit-on-reuse, 1 = fast small-region pool (retain-committed)
    size_t memory_cap = 131072; // default 128 KB aggregate cap across the entire pool
    size_t aggregate_committed = 0;
    size_t alloc_gran = 65536;

    ThreadReservationCache() {
        alloc_gran = getPlatformPageProvider().allocationGranularity();
        const char* envPol = std::getenv("TAMARIN_RCACHE_POLICY");
        if (envPol && *envPol) {
            policy = std::atoi(envPol);
        }
        const char* envCap = std::getenv("TAMARIN_RCACHE_MEMCAP");
        if (envCap && *envCap) {
            size_t cap = static_cast<size_t>(std::strtoull(envCap, nullptr, 10));
            if (cap > 0) memory_cap = cap;
        }
    }

    ~ThreadReservationCache() {
        clear();
    }

    void clear() {
        for (auto& s : slots) {
            if (s.base) {
                getPlatformPageProvider().release(s.base);
                s.base = nullptr;
                s.reserved_size = 0;
                s.committed_size = 0;
            }
        }
        aggregate_committed = 0;
    }

    void* acquire(size_t needed_res, size_t* out_reserved, size_t* out_committed) {
        // Fast-path: check slot 0 first (hot single-region churn)
        if (slots[0].base != nullptr && slots[0].reserved_size >= needed_res) {
            void* base = slots[0].base;
            size_t rsize = slots[0].reserved_size;
            size_t csize = slots[0].committed_size;
            if (out_reserved) *out_reserved = rsize;
            if (out_committed) *out_committed = csize;
            slots[0].base = nullptr;
            slots[0].reserved_size = 0;
            slots[0].committed_size = 0;
            if (aggregate_committed >= csize) {
                aggregate_committed -= csize;
            } else {
                aggregate_committed = 0;
            }
            return base;
        }

        size_t best_idx = MAX_SLOTS;
        size_t best_diff = static_cast<size_t>(-1);
        for (size_t i = 1; i < MAX_SLOTS; ++i) {
            if (slots[i].base != nullptr && slots[i].reserved_size >= needed_res) {
                size_t diff = slots[i].reserved_size - needed_res;
                if (diff < best_diff) {
                    best_diff = diff;
                    best_idx = i;
                    if (diff == 0) break;
                }
            }
        }
        if (best_idx < MAX_SLOTS) {
            void* base = slots[best_idx].base;
            size_t rsize = slots[best_idx].reserved_size;
            size_t csize = slots[best_idx].committed_size;
            if (out_reserved) *out_reserved = rsize;
            if (out_committed) *out_committed = csize;
            slots[best_idx].base = nullptr;
            slots[best_idx].reserved_size = 0;
            slots[best_idx].committed_size = 0;
            if (aggregate_committed >= csize) {
                aggregate_committed -= csize;
            } else {
                aggregate_committed = 0;
            }
            return base;
        }
        if (out_reserved) *out_reserved = 0;
        if (out_committed) *out_committed = 0;
        return nullptr;
    }

    int32_t put(void* base, size_t reserved_size, size_t committed_size, size_t used_size) {
        if (!base) return 0;

        // Large regions (> allocationGranularity) bypass the small-region pool
        if (reserved_size > alloc_gran) {
            getPlatformPageProvider().release(base);
            return 0;
        }

        if (policy == 0) {
            // Policy 0: Decommit on reuse (0 commit charge retained)
            if (committed_size > 0) {
                getPlatformPageProvider().decommit(base, 0, committed_size);
            }
            for (size_t i = 0; i < MAX_SLOTS; ++i) {
                if (slots[i].base == nullptr) {
                    slots[i].base = base;
                    slots[i].reserved_size = reserved_size;
                    slots[i].committed_size = 0;
                    return 1;
                }
            }
            getPlatformPageProvider().release(base);
            return 0;
        }

        // Policy 1: Fast Small-Region Pool (retains already-committed blocks)
        // 1. Restore zero-initialization guarantee for used bytes before pooling
        size_t bytes_to_zero = std::min(used_size > 0 ? used_size : committed_size, committed_size);
        if (bytes_to_zero > 0) {
            std::memset(base, 0, bytes_to_zero);
        }

        // 2. Check aggregate committed cap across the entire pool
        if (aggregate_committed + committed_size > memory_cap) {
            // Decommit this block down to 0 so aggregate cap is strictly enforced
            if (committed_size > 0) {
                getPlatformPageProvider().decommit(base, 0, committed_size);
                committed_size = 0;
            }
        }

        // 3. Fast-path: put into slot 0 if free
        if (slots[0].base == nullptr) {
            slots[0].base = base;
            slots[0].reserved_size = reserved_size;
            slots[0].committed_size = committed_size;
            aggregate_committed += committed_size;
            return 1;
        }

        // 4. Find another available slot
        for (size_t i = 1; i < MAX_SLOTS; ++i) {
            if (slots[i].base == nullptr) {
                slots[i].base = base;
                slots[i].reserved_size = reserved_size;
                slots[i].committed_size = committed_size;
                aggregate_committed += committed_size;
                return 1;
            }
        }

        // Pool is full; release reservation to OS
        getPlatformPageProvider().release(base);
        return 0;
    }
};

static thread_local ThreadReservationCache t_rcache;

// ── Thread-Local Large-Chunk and Growth-Chunk Reuse Pool ───────────────────────

struct ThreadChunkCache {
    struct Slot {
        void* base = nullptr;
        size_t reserved_size = 0;
        size_t committed_size = 0;
    };
    static constexpr size_t MAX_SLOTS = 8;
    Slot slots[MAX_SLOTS];
    int32_t policy = 1; // 1 = retain-committed, 0 = decommit-on-reuse
    size_t memory_cap = 4194304; // 4 MB default aggregate committed cap
    size_t aggregate_committed = 0;

    ThreadChunkCache() {
        const char* envPol = std::getenv("TAMARIN_CHUNK_CACHE_POLICY");
        if (envPol && *envPol) {
            policy = std::atoi(envPol);
        }
        const char* envCap = std::getenv("TAMARIN_CHUNK_CACHE_MEMCAP");
        if (envCap && *envCap) {
            size_t cap = static_cast<size_t>(std::strtoull(envCap, nullptr, 10));
            if (cap > 0) memory_cap = cap;
        }
    }

    ~ThreadChunkCache() {
        clear();
    }

    void clear() {
        for (auto& s : slots) {
            if (s.base) {
                getPlatformPageProvider().release(s.base);
                s.base = nullptr;
                s.reserved_size = 0;
                s.committed_size = 0;
            }
        }
        aggregate_committed = 0;
    }

    void* acquire(size_t needed_res, size_t* out_reserved, size_t* out_committed) {
        size_t best_idx = MAX_SLOTS;
        size_t best_diff = static_cast<size_t>(-1);
        for (size_t i = 0; i < MAX_SLOTS; ++i) {
            if (slots[i].base != nullptr && slots[i].reserved_size >= needed_res) {
                size_t diff = slots[i].reserved_size - needed_res;
                if (diff < best_diff) {
                    best_diff = diff;
                    best_idx = i;
                    if (diff == 0) break;
                }
            }
        }
        if (best_idx < MAX_SLOTS) {
            void* base = slots[best_idx].base;
            size_t rsize = slots[best_idx].reserved_size;
            size_t csize = slots[best_idx].committed_size;
            if (out_reserved) *out_reserved = rsize;
            if (out_committed) *out_committed = csize;
            slots[best_idx].base = nullptr;
            slots[best_idx].reserved_size = 0;
            slots[best_idx].committed_size = 0;
            if (aggregate_committed >= csize) {
                aggregate_committed -= csize;
            } else {
                aggregate_committed = 0;
            }
            return base;
        }
        if (out_reserved) *out_reserved = 0;
        if (out_committed) *out_committed = 0;
        return nullptr;
    }

    int32_t put(void* base, size_t reserved_size, size_t committed_size, size_t used_size) {
        if (!base) return 0;

        static constexpr size_t MAX_CHUNK_SIZE = 8 * 1024 * 1024; // 8 MB max
        if (reserved_size > MAX_CHUNK_SIZE || reserved_size > memory_cap) {
            getPlatformPageProvider().release(base);
            return 0;
        }

        if (policy == 0) {
            if (committed_size > 0) {
                getPlatformPageProvider().decommit(base, 0, committed_size);
            }
            for (size_t i = 0; i < MAX_SLOTS; ++i) {
                if (slots[i].base == nullptr) {
                    slots[i].base = base;
                    slots[i].reserved_size = reserved_size;
                    slots[i].committed_size = 0;
                    return 1;
                }
            }
            getPlatformPageProvider().release(base);
            return 0;
        }

        // Policy 1: Retain committed
        size_t bytes_to_zero = std::min(used_size > 0 ? used_size : committed_size, committed_size);
        if (bytes_to_zero > 0) {
            std::memset(base, 0, bytes_to_zero);
        }

        if (aggregate_committed + committed_size > memory_cap) {
            if (committed_size > 0) {
                getPlatformPageProvider().decommit(base, 0, committed_size);
                committed_size = 0;
            }
        }

        for (size_t i = 0; i < MAX_SLOTS; ++i) {
            if (slots[i].base == nullptr) {
                slots[i].base = base;
                slots[i].reserved_size = reserved_size;
                slots[i].committed_size = committed_size;
                aggregate_committed += committed_size;
                return 1;
            }
        }

        getPlatformPageProvider().release(base);
        return 0;
    }
};

static thread_local ThreadChunkCache t_chunk_cache;

// ── Thread-Local Sidecar Chunk Reuse Pool ────────────────────────────────────

struct ThreadSidecarCache {
    void* cached_chunk = nullptr;

    ~ThreadSidecarCache() {
        clear();
    }

    void clear() {
        if (cached_chunk) {
            std::free(cached_chunk);
            cached_chunk = nullptr;
        }
    }

    void* acquire() {
        void* p = cached_chunk;
        cached_chunk = nullptr;
        return p;
    }

    void put(void* chunk) {
        if (!chunk) return;
        if (!cached_chunk) {
            cached_chunk = chunk;
        } else {
            std::free(chunk);
        }
    }
};

static thread_local ThreadSidecarCache t_sidecar_cache;

} // namespace tamarin::runtime

// ── C-linkage runtime exports ────────────────────────────────────────────────

extern "C" {

void* __tamarin_runtime_page_reserve(size_t size) {
    return tamarin::runtime::getPlatformPageProvider().reserve(size);
}

void* __tamarin_runtime_page_commit(void* base, size_t offset, size_t size) {
    if (tamarin::runtime::getPlatformPageProvider().commit(base, offset, size)) {
        return static_cast<char*>(base) + offset;
    }
    return nullptr;
}

int32_t __tamarin_runtime_page_decommit(void* base, size_t offset, size_t size) {
    return tamarin::runtime::getPlatformPageProvider().decommit(base, offset, size) ? 1 : 0;
}

int32_t __tamarin_runtime_page_release(void* base) {
    return tamarin::runtime::getPlatformPageProvider().release(base) ? 1 : 0;
}

size_t __tamarin_runtime_page_size() {
    return tamarin::runtime::getPlatformPageProvider().pageSize();
}

size_t __tamarin_runtime_allocation_granularity() {
    return tamarin::runtime::getPlatformPageProvider().allocationGranularity();
}

size_t __tamarin_runtime_working_set_bytes() {
    return tamarin::runtime::getPlatformPageProvider().getResidentWorkingSet();
}

void* __tamarin_runtime_rcache_acquire(size_t needed_res, size_t* out_reserved, size_t* out_committed) {
    return tamarin::runtime::t_rcache.acquire(needed_res, out_reserved, out_committed);
}

int32_t __tamarin_runtime_rcache_put(void* base, size_t reserved_size, size_t committed_size, size_t used_size) {
    return tamarin::runtime::t_rcache.put(base, reserved_size, committed_size, used_size);
}

size_t __tamarin_runtime_rcache_get_aggregate_committed() {
    return tamarin::runtime::t_rcache.aggregate_committed;
}

void __tamarin_runtime_rcache_clear() {
    tamarin::runtime::t_rcache.clear();
}

void __tamarin_runtime_rcache_set_policy(int32_t policy, size_t memory_cap) {
    tamarin::runtime::t_rcache.policy = policy;
    if (memory_cap > 0) tamarin::runtime::t_rcache.memory_cap = memory_cap;
}

int32_t __tamarin_runtime_rcache_get_policy() {
    return tamarin::runtime::t_rcache.policy;
}

void* __tamarin_runtime_chunk_cache_acquire(size_t needed_res, size_t* out_reserved, size_t* out_committed) {
    return tamarin::runtime::t_chunk_cache.acquire(needed_res, out_reserved, out_committed);
}

int32_t __tamarin_runtime_chunk_cache_put(void* base, size_t reserved_size, size_t committed_size, size_t used_size) {
    return tamarin::runtime::t_chunk_cache.put(base, reserved_size, committed_size, used_size);
}

void __tamarin_runtime_chunk_cache_clear() {
    tamarin::runtime::t_chunk_cache.clear();
}

void __tamarin_runtime_chunk_cache_set_policy(int32_t policy, size_t memory_cap) {
    tamarin::runtime::t_chunk_cache.policy = policy;
    if (memory_cap > 0) tamarin::runtime::t_chunk_cache.memory_cap = memory_cap;
}

int32_t __tamarin_runtime_chunk_cache_get_policy() {
    return tamarin::runtime::t_chunk_cache.policy;
}

size_t __tamarin_runtime_chunk_cache_get_aggregate_committed() {
    return tamarin::runtime::t_chunk_cache.aggregate_committed;
}

void* __tamarin_runtime_sidecar_cache_acquire() {
    return tamarin::runtime::t_sidecar_cache.acquire();
}

void __tamarin_runtime_sidecar_cache_put(void* chunk) {
    tamarin::runtime::t_sidecar_cache.put(chunk);
}

void __tamarin_runtime_sidecar_cache_clear() {
    tamarin::runtime::t_sidecar_cache.clear();
}

uint64_t __tamarin_runtime_get_reserve_count() {
    return tamarin::runtime::s_stats.reserve_count.load(std::memory_order_relaxed);
}

uint64_t __tamarin_runtime_get_commit_count() {
    return tamarin::runtime::s_stats.commit_count.load(std::memory_order_relaxed);
}

uint64_t __tamarin_runtime_get_decommit_count() {
    return tamarin::runtime::s_stats.decommit_count.load(std::memory_order_relaxed);
}

uint64_t __tamarin_runtime_get_release_count() {
    return tamarin::runtime::s_stats.release_count.load(std::memory_order_relaxed);
}

void __tamarin_runtime_reset_counters() {
    tamarin::runtime::s_stats.reserve_count.store(0, std::memory_order_relaxed);
    tamarin::runtime::s_stats.commit_count.store(0, std::memory_order_relaxed);
    tamarin::runtime::s_stats.decommit_count.store(0, std::memory_order_relaxed);
    tamarin::runtime::s_stats.release_count.store(0, std::memory_order_relaxed);
}

void __tamarin_runtime_set_page_provider(tamarin::runtime::MemoryPageProvider* provider) {
    tamarin::runtime::setPlatformPageProvider(provider);
}

tamarin::runtime::MemoryPageProvider* __tamarin_runtime_get_page_provider() {
    return &tamarin::runtime::getPlatformPageProvider();
}

// ── Generational Slab Runtime Implementation ────────────────────────────────

static bool find_slab_slot(tamarin::runtime::TamarinSlab* slab, uint32_t slot,
                           uint32_t** out_gen_ptr, uint8_t** out_slot_ptr) {
    if (!slab || slot >= slab->capacity) return false;
    uint32_t cap0 = slab->chunks ? slab->chunks->slot_offset : static_cast<uint32_t>(slab->capacity);
    if (slot < cap0) {
        if (out_gen_ptr) *out_gen_ptr = &slab->generations[slot];
        if (out_slot_ptr) *out_slot_ptr = slab->slots + static_cast<size_t>(slot) * slab->slot_stride;
        return true;
    }
    for (auto* chunk = slab->chunks; chunk != nullptr; chunk = chunk->next) {
        if (slot >= chunk->slot_offset && slot < chunk->slot_offset + chunk->capacity) {
            uint32_t local = slot - chunk->slot_offset;
            if (out_gen_ptr) *out_gen_ptr = &chunk->generations[local];
            if (out_slot_ptr) *out_slot_ptr = chunk->slots + static_cast<size_t>(local) * slab->slot_stride;
            return true;
        }
    }
    return false;
}

static void slab_grow_chunk(tamarin::runtime::TamarinSlab* slab) {
    uint32_t chunk_cap = static_cast<uint32_t>(slab->capacity);
    if (chunk_cap < 64) chunk_cap = 64;
    if (chunk_cap > 65536) chunk_cap = 65536;

    auto* chunk = new tamarin::runtime::TamarinSlabChunk();
    size_t gens_bytes = static_cast<size_t>(chunk_cap) * sizeof(uint32_t);
    size_t gens_bytes_aligned = (gens_bytes + 15) & ~15ULL;
    size_t slots_bytes = static_cast<size_t>(chunk_cap) * slab->slot_stride;
    size_t total_memory = gens_bytes_aligned + slots_bytes;

    size_t gran = tamarin::runtime::getPlatformPageProvider().allocationGranularity();
    size_t page_sz = tamarin::runtime::getPlatformPageProvider().pageSize();
    size_t total_res = (total_memory + gran - 1) & ~(gran - 1);
    size_t total_com = (total_memory + page_sz - 1) & ~(page_sz - 1);

    size_t c_res = 0, c_com = 0;
    void* base = tamarin::runtime::t_chunk_cache.acquire(total_res, &c_res, &c_com);
    if (!base) {
        base = tamarin::runtime::getPlatformPageProvider().reserve(total_res);
        tamarin::runtime::getPlatformPageProvider().commit(base, 0, total_com);
        c_res = total_res;
        c_com = total_com;
    } else if (c_com < total_com) {
        tamarin::runtime::getPlatformPageProvider().commit(base, c_com, total_com - c_com);
        c_com = total_com;
    }

    chunk->generations = reinterpret_cast<uint32_t*>(base);
    chunk->slots = reinterpret_cast<uint8_t*>(base) + gens_bytes_aligned;
    chunk->capacity = chunk_cap;
    chunk->count = 0;
    chunk->slot_offset = static_cast<uint32_t>(slab->capacity);
    chunk->memory_base = base;
    chunk->memory_reserved = c_res;
    chunk->memory_committed = c_com;
    chunk->next = nullptr;

    std::memset(chunk->generations, 0, gens_bytes);

    for (uint32_t i = 0; i < chunk_cap; ++i) {
        uint32_t nxt = (i + 1 < chunk_cap) ? (chunk->slot_offset + i + 1) : 0xFFFFFFFF;
        *reinterpret_cast<uint32_t*>(chunk->slots + static_cast<size_t>(i) * slab->slot_stride) = nxt;
    }

    chunk->free_head = chunk->slot_offset;

    if (!slab->chunks) {
        slab->chunks = chunk;
    } else {
        auto* curr = slab->chunks;
        while (curr->next) curr = curr->next;
        curr->next = chunk;
    }

    slab->free_head = chunk->slot_offset;
    slab->capacity += chunk_cap;
}

void __tamarin_slab_init(tamarin::runtime::TamarinSlab* slab, size_t elem_size, size_t elem_align, size_t initial_cap) {
    if (!slab) return;
    std::memset(slab, 0, sizeof(tamarin::runtime::TamarinSlab));

    if (initial_cap == 0) initial_cap = 64;
    size_t stride = std::max(elem_size, static_cast<size_t>(4));
    size_t align = std::max(elem_align, static_cast<size_t>(4));
    size_t rem = stride % align;
    if (rem != 0) stride += (align - rem);

    size_t gens_bytes = initial_cap * sizeof(uint32_t);
    size_t gens_bytes_aligned = (gens_bytes + 15) & ~15ULL;
    size_t slots_bytes = initial_cap * stride;
    size_t total_memory = gens_bytes_aligned + slots_bytes;

    size_t gran = tamarin::runtime::getPlatformPageProvider().allocationGranularity();
    size_t page_sz = tamarin::runtime::getPlatformPageProvider().pageSize();
    size_t total_res = (total_memory + gran - 1) & ~(gran - 1);
    size_t total_com = (total_memory + page_sz - 1) & ~(page_sz - 1);

    size_t c_res = 0, c_com = 0;
    void* base = tamarin::runtime::t_chunk_cache.acquire(total_res, &c_res, &c_com);
    if (!base) {
        base = tamarin::runtime::getPlatformPageProvider().reserve(total_res);
        tamarin::runtime::getPlatformPageProvider().commit(base, 0, total_com);
        c_res = total_res;
        c_com = total_com;
    } else if (c_com < total_com) {
        tamarin::runtime::getPlatformPageProvider().commit(base, c_com, total_com - c_com);
        c_com = total_com;
    }

    slab->generations = reinterpret_cast<uint32_t*>(base);
    slab->slots = reinterpret_cast<uint8_t*>(base) + gens_bytes_aligned;
    std::memset(slab->generations, 0, gens_bytes);

    for (size_t i = 0; i < initial_cap; ++i) {
        uint32_t nxt = (i + 1 < initial_cap) ? static_cast<uint32_t>(i + 1) : 0xFFFFFFFF;
        *reinterpret_cast<uint32_t*>(slab->slots + i * stride) = nxt;
    }

    slab->capacity = initial_cap;
    slab->count = 0;
    slab->free_head = 0;
    slab->elem_size = elem_size;
    slab->slot_stride = stride;
    slab->chunks = nullptr;
    slab->memory_base = base;
    slab->memory_reserved = c_res;
    slab->memory_committed = c_com;
}

uint64_t __tamarin_slab_insert(tamarin::runtime::TamarinSlab* slab, const void* elem_val) {
    if (!slab) return 0;
    if (slab->free_head == 0xFFFFFFFF) {
        slab_grow_chunk(slab);
    }

    uint32_t slot = static_cast<uint32_t>(slab->free_head);
    uint32_t* gen_ptr = nullptr;
    uint8_t* slot_ptr = nullptr;
    if (!find_slab_slot(slab, slot, &gen_ptr, &slot_ptr)) {
        return 0;
    }

    slab->free_head = *reinterpret_cast<uint32_t*>(slot_ptr);

    uint32_t cur_gen = *gen_ptr;
    cur_gen++; // advances to odd = active
    *gen_ptr = cur_gen;

    std::memset(slot_ptr, 0, slab->slot_stride);
    if (elem_val) {
        std::memcpy(slot_ptr, elem_val, slab->elem_size);
    }

    slab->count++;
    return (static_cast<uint64_t>(cur_gen) << 32) | static_cast<uint64_t>(slot);
}

void __tamarin_slab_panic(uint32_t slot, uint32_t handle_gen, uint32_t actual_gen) {
    char buf[256];
    int len = std::snprintf(buf, sizeof(buf),
        "\nTAMARIN Panic: Invalid generational handle access! Slot: %u, Handle Gen: %u, Slot Gen: %u (use-after-free or invalid handle)\n",
        slot, handle_gen, actual_gen);
#ifdef _WIN32
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), buf, static_cast<DWORD>(len), &written, nullptr);
#else
    std::fwrite(buf, 1, len, stderr);
#endif
    std::exit(1);
}

int32_t __tamarin_slab_is_valid(const tamarin::runtime::TamarinSlab* slab, uint64_t handle) {
    if (!slab) return 0;
    uint32_t slot = static_cast<uint32_t>(handle & 0xFFFFFFFF);
    uint32_t gen = static_cast<uint32_t>(handle >> 32);
    if (gen == 0 || slot >= slab->capacity) return 0;

    uint32_t* gen_ptr = nullptr;
    uint8_t* slot_ptr = nullptr;
    if (!find_slab_slot(const_cast<tamarin::runtime::TamarinSlab*>(slab), slot, &gen_ptr, &slot_ptr)) {
        return 0;
    }

    if (*gen_ptr == gen && (*gen_ptr & 1) != 0) return 1;
    return 0;
}

void* __tamarin_slab_get(tamarin::runtime::TamarinSlab* slab, uint64_t handle) {
    if (!slab) __tamarin_slab_panic(0, 0, 0);
    uint32_t slot = static_cast<uint32_t>(handle & 0xFFFFFFFF);
    uint32_t gen = static_cast<uint32_t>(handle >> 32);
    if (gen == 0 || slot >= slab->capacity) {
        __tamarin_slab_panic(slot, gen, 0);
    }

    uint32_t* gen_ptr = nullptr;
    uint8_t* slot_ptr = nullptr;
    if (!find_slab_slot(slab, slot, &gen_ptr, &slot_ptr)) {
        __tamarin_slab_panic(slot, gen, 0);
    }

    if (*gen_ptr != gen || (*gen_ptr & 1) == 0) {
        __tamarin_slab_panic(slot, gen, *gen_ptr);
    }

    return slot_ptr;
}

int32_t __tamarin_slab_try_get(const tamarin::runtime::TamarinSlab* slab, uint64_t handle, void* out_val) {
    if (!__tamarin_slab_is_valid(slab, handle)) return 0;
    uint32_t slot = static_cast<uint32_t>(handle & 0xFFFFFFFF);
    uint32_t* gen_ptr = nullptr;
    uint8_t* slot_ptr = nullptr;
    find_slab_slot(const_cast<tamarin::runtime::TamarinSlab*>(slab), slot, &gen_ptr, &slot_ptr);
    if (out_val) {
        std::memcpy(out_val, slot_ptr, slab->elem_size);
    }
    return 1;
}

int32_t __tamarin_slab_remove(tamarin::runtime::TamarinSlab* slab, uint64_t handle, void (*dtor)(void*)) {
    if (!__tamarin_slab_is_valid(slab, handle)) return 0;
    uint32_t slot = static_cast<uint32_t>(handle & 0xFFFFFFFF);
    uint32_t* gen_ptr = nullptr;
    uint8_t* slot_ptr = nullptr;
    find_slab_slot(slab, slot, &gen_ptr, &slot_ptr);

    if (dtor) {
        dtor(slot_ptr);
    }

    uint32_t new_gen = *gen_ptr + 1; // advances to even = free
    if (new_gen >= 0xFFFFFFFE) {
        *gen_ptr = 0xFFFFFFFF; // permanently retired
        slab->count--;
        return 1;
    }

    *gen_ptr = new_gen;
    *reinterpret_cast<uint32_t*>(slot_ptr) = static_cast<uint32_t>(slab->free_head);
    slab->free_head = slot;
    slab->count--;
    return 1;
}

uint64_t __tamarin_slab_count(const tamarin::runtime::TamarinSlab* slab) {
    return slab ? slab->count : 0;
}

uint64_t __tamarin_slab_capacity(const tamarin::runtime::TamarinSlab* slab) {
    return slab ? slab->capacity : 0;
}

void __tamarin_slab_clear(tamarin::runtime::TamarinSlab* slab, void (*dtor)(void*)) {
    if (!slab) return;

    // Destruct secondary chunks active items in LIFO reverse order
    std::vector<tamarin::runtime::TamarinSlabChunk*> chunk_list;
    for (auto* c = slab->chunks; c != nullptr; c = c->next) {
        chunk_list.push_back(c);
    }
    for (auto it = chunk_list.rbegin(); it != chunk_list.rend(); ++it) {
        auto* c = *it;
        for (int64_t i = static_cast<int64_t>(c->capacity) - 1; i >= 0; --i) {
            if ((c->generations[i] & 1) != 0) {
                if (dtor) {
                    dtor(c->slots + static_cast<size_t>(i) * slab->slot_stride);
                }
                c->generations[i]++;
            }
        }
    }

    // Destruct primary chunk active items in reverse order
    uint32_t cap0 = slab->chunks ? slab->chunks->slot_offset : static_cast<uint32_t>(slab->capacity);
    for (int64_t i = static_cast<int64_t>(cap0) - 1; i >= 0; --i) {
        if ((slab->generations[i] & 1) != 0) {
            if (dtor) {
                dtor(slab->slots + static_cast<size_t>(i) * slab->slot_stride);
            }
            slab->generations[i]++;
        }
    }

    // Rebuild free list
    slab->free_head = 0xFFFFFFFF;

    // From back to front
    for (auto it = chunk_list.rbegin(); it != chunk_list.rend(); ++it) {
        auto* c = *it;
        for (int64_t i = static_cast<int64_t>(c->capacity) - 1; i >= 0; --i) {
            if (c->generations[i] < 0xFFFFFFFE) {
                uint32_t s_idx = c->slot_offset + static_cast<uint32_t>(i);
                *reinterpret_cast<uint32_t*>(c->slots + static_cast<size_t>(i) * slab->slot_stride) = static_cast<uint32_t>(slab->free_head);
                slab->free_head = s_idx;
            }
        }
    }
    for (int64_t i = static_cast<int64_t>(cap0) - 1; i >= 0; --i) {
        if (slab->generations[i] < 0xFFFFFFFE) {
            *reinterpret_cast<uint32_t*>(slab->slots + static_cast<size_t>(i) * slab->slot_stride) = static_cast<uint32_t>(slab->free_head);
            slab->free_head = static_cast<uint32_t>(i);
        }
    }

    slab->count = 0;
}

void __tamarin_slab_destroy(tamarin::runtime::TamarinSlab* slab, void (*dtor)(void*)) {
    if (!slab) return;
    if (slab->count > 0 && dtor) {
        __tamarin_slab_clear(slab, dtor);
    }

    auto* curr = slab->chunks;
    while (curr) {
        auto* nxt = curr->next;
        tamarin::runtime::t_chunk_cache.put(curr->memory_base, curr->memory_reserved, curr->memory_committed, curr->memory_committed);
        delete curr;
        curr = nxt;
    }
    slab->chunks = nullptr;

    if (slab->memory_base) {
        tamarin::runtime::t_chunk_cache.put(slab->memory_base, slab->memory_reserved, slab->memory_committed, slab->memory_committed);
        slab->memory_base = nullptr;
    }

    std::memset(slab, 0, sizeof(tamarin::runtime::TamarinSlab));
}

} // extern "C"
