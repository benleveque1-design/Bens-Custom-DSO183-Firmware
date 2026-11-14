#include "memdiag.h"

#include <stddef.h>
#include "stm32f1xx.h"

/* Linker symbols (addresses). */
extern char _end;       /* end of .bss / start of heap in ._user_heap_stack */
extern char _estack;    /* top of RAM / initial MSP */
extern char _min_heap_size;
extern char _min_stack_size;

#define STACK_PAINT_WORD  MEMDIAG_STACK_PATTERN
/* Leave margin above current SP untouched when painting at call time. */
#define STACK_PAINT_GUARD 128U

static volatile uint32_t s_heap_highwater;
static volatile void    *s_heap_end;
static volatile uint32_t s_msp_low_water;

static inline uint32_t heap_base_addr(void) { return (uint32_t)(uintptr_t)&_end; }
static inline uint32_t heap_size_bytes(void) { return (uint32_t)(uintptr_t)&_min_heap_size; }
static inline uint32_t stack_size_bytes(void) { return (uint32_t)(uintptr_t)&_min_stack_size; }
static inline uint32_t heap_limit_addr(void) { return heap_base_addr() + heap_size_bytes(); }
static inline uint32_t stack_top_addr(void) { return (uint32_t)(uintptr_t)&_estack; }
static inline uint32_t stack_bottom_addr(void) { return heap_limit_addr(); }

uint32_t memdiag_msp(void)
{
    uint32_t sp;
    __asm volatile ("mrs %0, msp" : "=r"(sp));
    return sp;
}

uint32_t memdiag_stack_bottom(void) { return stack_bottom_addr(); }
uint32_t memdiag_stack_top(void) { return stack_top_addr(); }

void memdiag_msp_sample(void)
{
    uint32_t sp = memdiag_msp();
    uint32_t pm = __get_PRIMASK();

    __disable_irq();
    if ((s_msp_low_water == 0U) || (sp < s_msp_low_water)) {
        s_msp_low_water = sp;
    }
    if (pm == 0U) {
        __enable_irq();
    }
}

uint32_t memdiag_msp_low_water(void)
{
    return s_msp_low_water;
}

uint32_t memdiag_msp_free_min(void)
{
    uint32_t lw = s_msp_low_water;
    uint32_t bottom = stack_bottom_addr();
    if ((lw == 0U) || (lw == 0xFFFFFFFFUL) || (lw <= bottom)) {
        return 0U;
    }
    return lw - bottom;
}

void memdiag_stack_paint_early(void)
{
    uint32_t sp = memdiag_msp();
    uint32_t bottom = stack_bottom_addr();
    uint32_t paint_end = sp;

    /* Initialize MSP low-water baseline and capture current depth. */
    s_msp_low_water = stack_top_addr();
    memdiag_msp_sample();

    if (paint_end > STACK_PAINT_GUARD) {
        paint_end -= STACK_PAINT_GUARD;
    } else {
        paint_end = bottom;
    }
    /* Align down to word boundary. */
    paint_end &= ~3UL;
    bottom = (bottom + 3UL) & ~3UL;

    for (uint32_t *p = (uint32_t *)(uintptr_t)bottom;
         (uint32_t)(uintptr_t)p < paint_end; p++) {
        *p = STACK_PAINT_WORD;
    }
}

uint32_t memdiag_stack_free_now(void)
{
    uint32_t sp = memdiag_msp();
    uint32_t bottom = stack_bottom_addr();
    if (sp <= bottom) { return 0U; }
    return sp - bottom;
}

uint32_t memdiag_stack_free_min(void)
{
    uint32_t bottom = (stack_bottom_addr() + 3UL) & ~3UL;
    uint32_t top = stack_top_addr() & ~3UL;
    uint32_t p = bottom;

    /* Count intact pattern words from bottom upward (lowest untouched). */
    while (p < top) {
        if (*(volatile uint32_t *)(uintptr_t)p != STACK_PAINT_WORD) {
            break;
        }
        p += 4UL;
    }
    if (p <= bottom) { return 0U; }
    return p - bottom;
}

uint32_t memdiag_heap_base(void) { return heap_base_addr(); }
uint32_t memdiag_heap_limit(void) { return heap_limit_addr(); }

void memdiag_heap_note(void *new_end)
{
    uint32_t base = heap_base_addr();
    uint32_t endv = (uint32_t)(uintptr_t)new_end;
    uint32_t used = 0U;
    if (endv >= base) {
        used = endv - base;
    }
    if (used > s_heap_highwater) {
        s_heap_highwater = used;
    }
    s_heap_end = new_end;
}

uint32_t memdiag_heap_used(void)
{
    uint32_t base = heap_base_addr();
    uint32_t endv = (uint32_t)(uintptr_t)s_heap_end;
    if ((s_heap_end == NULL) || (endv < base)) {
        return 0U;
    }
    return endv - base;
}

uint32_t memdiag_heap_highwater(void)
{
    return s_heap_highwater;
}
