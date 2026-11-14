/* Minimal _sbrk override for heap tracking (Stage 0 instrumentation).
 * Compatible with newlib nano/libnosys expectations; bounded to linker heap.
 */
#include "memdiag.h"

#include <errno.h>
#include <stdint.h>
#include <stddef.h>

extern char _end;
extern char _min_heap_size;

void *_sbrk(ptrdiff_t incr)
{
    static char *heap_end;
    char *prev;
    const char *heap_limit = (const char *)&_end + (uint32_t)(uintptr_t)&_min_heap_size;

    if (heap_end == 0) {
        heap_end = &_end;
        memdiag_heap_note((void *)heap_end);
    }
    prev = heap_end;
    if ((heap_end + incr) > heap_limit) {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_end += incr;
    memdiag_heap_note((void *)heap_end);
    return (void *)prev;
}
