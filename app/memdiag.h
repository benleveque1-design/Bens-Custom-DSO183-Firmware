/* SRAM/stack/heap instrumentation (Stage 0/0.5, observability only).
 * No acquisition/buffer/timing/precision/functional behavior changes.
 */
#ifndef MEMDIAG_H
#define MEMDIAG_H

#include <stdint.h>

#define MEMDIAG_STACK_PATTERN 0xCDCDCDCDUL

/* Paint stack region early (call once, before deep call chains). Also
 * initializes MSP low-water tracking baseline. */
void memdiag_stack_paint_early(void);

/* Sample current MSP and update raw minimum-MSP low-water (ISR/thread safe).
 * May be called from thread or handler context. Keep out of timing-critical
 * sampling inner loops. */
void memdiag_msp_sample(void);

/* Current MSP (bytes). */
uint32_t memdiag_msp(void);

/* Raw minimum MSP observed since init (lowest stack pointer address). */
uint32_t memdiag_msp_low_water(void);

/* Derived headroom: bytes from stack bottom to minimum observed MSP. */
uint32_t memdiag_msp_free_min(void);

/* Stack reserved/painted window bottom and top (addresses). */
uint32_t memdiag_stack_bottom(void);
uint32_t memdiag_stack_top(void);

/* Bytes currently unused below MSP down to painted bottom (approx). */
uint32_t memdiag_stack_free_now(void);

/* Minimum-ever free bytes observed by watermark scan (low-water headroom). */
uint32_t memdiag_stack_free_min(void);

/* Heap: current break/end offset usage and high-water usage in bytes from heap base. */
uint32_t memdiag_heap_base(void);
uint32_t memdiag_heap_limit(void);
uint32_t memdiag_heap_used(void);
uint32_t memdiag_heap_highwater(void);

/* Update heap tracking from _sbrk (internal hook). */
void memdiag_heap_note(void *new_end);

#endif /* MEMDIAG_H */
