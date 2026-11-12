/* TIM3 register commit (firmware only - app/tim3_hw.c). */
#ifndef TIM3_HW_H
#define TIM3_HW_H

#include "tim3.h"

/* Programme TIM3 with cfg, latch it and start free-running with TRGO on
 * update.  Returns 0 on success, 1 for a NULL cfg. */
uint8_t tim3_hw_start(const struct tim3_config *cfg);

/* Stop the counter and switch TRGO off. */
void tim3_hw_stop(void);

#endif /* TIM3_HW_H */
