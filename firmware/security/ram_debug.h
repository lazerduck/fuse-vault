#ifndef FV_RAM_DEBUG_H
#define FV_RAM_DEBUG_H
#include <stdint.h>
typedef struct {
    uint32_t total_bytes, fixed_bytes, heap_reserved_bytes;
    uint32_t heap_peak_reserved_bytes, uncommitted_bytes;
} fv_ram_debug;
/* Heap reservation includes reusable freed allocations; not live allocation
 * totals. Fixed usage includes all reserved stacks, not stack high-water use. */
fv_ram_debug fv_ram_debug_read(void);
#endif
