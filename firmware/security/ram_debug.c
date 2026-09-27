#include "ram_debug.h"
#include "hardware/regs/addressmap.h"
#include <stdatomic.h>
#include <stddef.h>

extern char __end__, __HeapLimit;
extern void *__real__sbrk(int increment);
static atomic_uint heap_reserved, heap_peak_reserved;
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "RAM telemetry requires lock-free counters");

/* The SDK malloc wrappers serialize allocator calls. Observe its actual break
 * after success (including optimistic sbrk), without walking allocator state
 * from the USB core or adding any allocation inside the allocator path. */
void *__wrap__sbrk(int increment) {
    void *previous = __real__sbrk(increment);
    if (previous != (void *)-1) {
        uintptr_t current = (uintptr_t)__real__sbrk(0);
        uintptr_t start = (uintptr_t)&__end__;
        uintptr_t limit = (uintptr_t)&__HeapLimit;
        if (current >= start && current <= limit) {
            unsigned reserved = (unsigned)(current - start);
            unsigned peak = atomic_load_explicit(&heap_peak_reserved, memory_order_relaxed);
            if (reserved > peak)
                atomic_store_explicit(&heap_peak_reserved, reserved, memory_order_relaxed);
            atomic_store_explicit(&heap_reserved, reserved, memory_order_release);
        }
    }
    return previous;
}

fv_ram_debug fv_ram_debug_read(void) {
    uint32_t capacity = (uint32_t)((uintptr_t)&__HeapLimit - (uintptr_t)&__end__);
    uint32_t reserved = atomic_load_explicit(&heap_reserved, memory_order_acquire);
    uint32_t peak = atomic_load_explicit(&heap_peak_reserved, memory_order_relaxed);
    uint32_t total = SRAM_END - SRAM_BASE;
    return (fv_ram_debug){total, total - capacity, reserved, peak, capacity - reserved};
}
