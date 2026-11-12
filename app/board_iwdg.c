#include "board_iwdg.h"

#include "stm32f1xx_hal.h"

#define BOARD_IWDG_BASE 0x40003000UL
#define IWDG_KR        (*(volatile uint32_t *)(BOARD_IWDG_BASE + 0x00UL))
/* RM0008: KR 0x00, PR 0x04, RLR 0x08, SR 0x0C.  These two were swapped
 * here for months: the code wrote the 3-bit prescaler value into RLR and the
 * 3125-ms reload value into PR (which kept only its low 3 bits), so every
 * build programmed (RLR=5, PR=/128) = 19.2 ms instead of 10 s.  The bench
 * confirmed it directly - writing 0xABC to +4 read back 4 (3-bit PR) and
 * writing 3 to +8 read back 3 (12-bit RLR). */
#define IWDG_PR        (*(volatile uint32_t *)(BOARD_IWDG_BASE + 0x04UL))
#define IWDG_RLR       (*(volatile uint32_t *)(BOARD_IWDG_BASE + 0x08UL))
#define IWDG_SR        (*(volatile uint32_t *)(BOARD_IWDG_BASE + 0x0CUL))

#define IWDG_KEY_RELOAD   0x0000AAAAUL
#define IWDG_KEY_ENABLE   0x0000CCCCUL
#define IWDG_KEY_UNLOCK   0x00005555UL


#define LSI_NOMINAL_HZ    40000UL

/* Prescaler -> divider.  IWDG_PR[2:0]: 0=/4 1=/8 2=/16 3=/32 4=/64 5=/128 6=/256 7=/512 */
static const uint32_t k_pr_div[8] = {4U, 8U, 16U, 32U, 64U, 128U, 256U, 512U};

/* Bounded wait for one IWDG update flag to self-clear.  Returns 0 when it
 * cleared, 1 if it is still set after the spin budget (the caller keeps going:
 * a stuck flag means the write was probably dropped, and refusing to enable
 * the watchdog would be worse than a short period). */
static uint8_t iwdg_wait_clear(uint32_t mask)
{
    uint32_t guard = 100000U;

    while (((IWDG_SR & mask) != 0UL) && (guard-- != 0U)) {
        /* spin */
    }
    return ((IWDG_SR & mask) != 0UL) ? 1U : 0U;
}

void board_iwdg_init(uint32_t timeout_ms)
{
    uint32_t pr = 0U;
    uint32_t div = 4U;
    uint32_t reload;

    if (timeout_ms < 10U) {
        timeout_ms = 10U;
    }

    /* Choose the smallest prescaler that gives a reload value <= 0xFFF. */
    for (pr = 0U; pr < 8U; pr++) {
        div = k_pr_div[pr];
        reload = (timeout_ms * LSI_NOMINAL_HZ) / (1000U * div);
        if (reload <= 0xFFFUL) {
            break;
        }
    }
    if (reload > 0xFFFUL) {
        reload = 0xFFFUL;
    }
    if (reload < 1U) {
        reload = 1U;
    }

    /* RM0008: PR and RLR are shadowed.  A write is IGNORED while the
     * corresponding update flag is still set, so each write needs its own
     * handshake - writing PR and RLR back to back (as an earlier revision
     * did) can silently leave RLR at its old value.  On the bench that
     * showed up as a ~19 ms watchdog instead of the intended 10 s, because
     * only the prescaler write took.  Wait with the real masks: PVU is
     * SR bit 2, RVU is SR bit 1. */
    IWDG_KR = IWDG_KEY_UNLOCK;      /* enable write access to PR and RLR */
    IWDG_PR = pr;
    (void)iwdg_wait_clear(IWDG_SR_PVU);
    IWDG_RLR = reload;
    (void)iwdg_wait_clear(IWDG_SR_RVU);
    IWDG_KR = IWDG_KEY_ENABLE;      /* start the watchdog */
    IWDG_KR = IWDG_KEY_RELOAD;      /* and load the new reload value */
}

void board_iwdg_feed(void)
{
    IWDG_KR = IWDG_KEY_RELOAD;
}
