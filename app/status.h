/* Liveness counters.
 *
 * The DSO183's own USB is power-only, and the ST-Link's UART pins are not wired
 * to the console, so there is no way to see the firmware's own output.  Instead
 * the firmware publishes a few counters in RAM, and the liveness check reads them
 * over SWD (tools/flash/live_check.py).  That is objective: the numbers either
 * advance or they do not.
 *
 * They are also genuinely useful in the field: a stuck counter tells you which
 * part of the firmware stopped.
 */
#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>

/* Incremented every SysTick tick (1 ms). */
extern volatile uint32_t status_heartbeat;

/* Incremented once per main-loop iteration. */
extern volatile uint32_t status_main_loops;

/* Build identity: a fixed magic plus a sequence number that is bumped for every
 * new build.  Reading it over SWD proves which firmware is actually running -
 * essential after a flash, because the page writer leaves the core halted and a
 * queued reset only fires once the core runs again. */
extern volatile uint32_t status_build_magic;
extern volatile uint32_t status_build_seq;

/* The controller ID read back over the bus (0x9341 for an ILI9341), or 0 if the
 * read failed.  This is the objective proof that the display bus works. */
extern volatile uint32_t status_display_id;

/* Raw ADC1 counts of the front-panel channels 0..7 (PA0..PA7), refreshed
 * by the diagnostic screen.  0xFFFF marks a conversion that did not
 * complete.  These are the empirical evidence for open question U-02a:
 * the channel that moves when a slide switch is flipped carries the
 * switch ladder. */
extern volatile uint16_t status_adc_raw[8];

/* Number of times the firmware has been started (1 after a cold boot, >1
 * after a watchdog reset - which is how a hang announces itself).
 *
 * The count itself lives in the .noinit section (RAM that the startup code
 * neither copies nor zeroes), because a counter in .data/.bss would be
 * wiped on every boot and could never show a reset loop.  A magic word
 * beside it distinguishes a cold boot (RAM contents undefined) from a warm
 * reset. */
extern volatile uint32_t status_boot_count;

/* Latch the boot counter into status_boot_count.  Call once, early. */
void status_boot_count_init(void);

/* Initialisation failure flags (bit field).  Every init step that can fail
 * records its bit here instead of spinning in an endless loop, so a failed
 * bring-up still reaches the diagnostic screen and the failure is VISIBLE
 * (on the panel and over SWD) instead of being a silent white screen. */
#define STATUS_ERR_OSC   0x01U   /* oscillator configuration failed     */
#define STATUS_ERR_CLOCK 0x02U   /* clock switch (PLL/64 MHz) failed    */
#define STATUS_ERR_UART  0x04U   /* console UART initialisation failed  */
#define STATUS_ERR_ADC   0x08U   /* ADC1 (front-panel input) failed     */
#define STATUS_ERR_ACQ   0x10U   /* acquisition engine failed to start  */
#define STATUS_ERR_MASK  0x1FU   /* all flags defined so far            */
extern volatile uint32_t status_init_errors;

static inline void status_init_error_set(uint32_t flag)
{
    status_init_errors |= flag;
}

static inline void status_heartbeat_inc(void)
{
    status_heartbeat++;
}

/* Acquisition (M5): the engine's liveness/health counters.  A DMA transfer
 * error counter that only climbs, or a trigger counter frozen while the DMA
 * transfer-complete counter keeps running, both tell a specific story over
 * SWD. */
extern volatile uint32_t status_acq_dma_tc;      /* buffer reloads        */
extern volatile uint32_t status_acq_triggers;    /* watchdog latches      */
extern volatile uint32_t status_acq_timeouts;    /* AUTO-fallback cycles  */
extern volatile uint32_t status_acq_hw_errors;   /* peripheral misbehaviour */

static inline void status_main_loop_inc(void)
{
    status_main_loops++;
}

static inline void status_acq_dma_tc_inc(void)
{
    status_acq_dma_tc++;
}

static inline void status_acq_triggers_inc(void)
{
    status_acq_triggers++;
}

static inline void status_acq_timeouts_inc(void)
{
    status_acq_timeouts++;
}

static inline void status_acq_hw_errors_inc(void)
{
    status_acq_hw_errors++;
}

#endif /* STATUS_H */
