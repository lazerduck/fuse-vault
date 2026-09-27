"""Exercise firmware RAM telemetry with a bounded fake allocator, no hardware."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='fv-ram-test-') as temporary:
        work = Path(temporary)
        registers = work / 'hardware/regs'
        registers.mkdir(parents=True)
        (registers / 'addressmap.h').write_text(
            '#define SRAM_BASE 0u\n#define SRAM_END 4096u\n')
        (work / 'test.c').write_text(r'''
#include "ram_debug.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
char fv_test_arena[4096];
__asm__(".globl __end__\n.set __end__, fv_test_arena + 1024\n"
        ".globl __HeapLimit\n.set __HeapLimit, fv_test_arena + 3584\n");
static int used, optimistic;
void *__real__sbrk(int increment) {
    int next = used + increment;
    if (next < 0 || (next > 2560 && (!optimistic || used == 2560)))
        return (void *)-1;
    void *previous = fv_test_arena + 1024 + used;
    used = next > 2560 ? 2560 : next;
    return previous;
}
extern void *__wrap__sbrk(int);
static void check(unsigned reserved, unsigned peak) {
    fv_ram_debug r = fv_ram_debug_read();
    assert(r.total_bytes == 4096 && r.fixed_bytes == 1536);
    assert(r.heap_reserved_bytes == reserved && r.heap_peak_reserved_bytes == peak);
    assert(r.uncommitted_bytes == 2560 - reserved);
    assert(r.fixed_bytes + r.heap_reserved_bytes + r.uncommitted_bytes == r.total_bytes);
}
int main(void) {
    check(0, 0);
    assert(__wrap__sbrk(256) == fv_test_arena + 1024);
    check(256, 256);
    assert(__wrap__sbrk(512) == fv_test_arena + 1280);
    /* No telemetry read at the high point: peak must survive shrink. */
    assert(__wrap__sbrk(-512) == fv_test_arena + 1792);
    check(256, 768);
    assert(__wrap__sbrk(3000) == (void *)-1);
    check(256, 768);
    optimistic = 1;
    assert(__wrap__sbrk(3000) == fv_test_arena + 1280);
    check(2560, 2560);
    assert(__wrap__sbrk(1) == (void *)-1);
    check(2560, 2560);
    puts("RAM accounting, unsampled peaks, shrink, failure and optimistic sbrk passed");
}
''')
        executable = work / 'ram-test'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(work), '-I', str(ROOT / 'firmware/security'),
                        str(work / 'test.c'), str(ROOT / 'firmware/security/ram_debug.c'),
                        '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
