/* Shared fixture for the host-side preview tools.
 *
 * One source of truth for the values the preview shows, so the text dump
 * (tools/dump_diag.c) and the pixel render (tools/panel_render.c) can never
 * drift apart.  The values mirror what a healthy board should display.
 */
#ifndef DIAG_FIXTURE_H
#define DIAG_FIXTURE_H

#include "diag.h"

#include <string.h>

/* When the preview is built inside the firmware build tree the REAL build
 * sequence is used, so the preview shows exactly what the panel will show
 * ("S=xx").  Outside that tree a fixed value keeps the tool standalone. */
#ifdef HAVE_BUILD_SEQ
#include "build_seq.h"
#define DIAG_FIXTURE_BUILD_SEQ ((uint8_t)STATUS_BUILD_SEQ)
#else
#define DIAG_FIXTURE_BUILD_SEQ 0xE0U
#endif

static inline void diag_fixture(struct diag_state *st)
{
    if (st == NULL) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->build_magic = 0x4453424DU;
    st->build_seq   = DIAG_FIXTURE_BUILD_SEQ;
    st->boot_count  = 1U;
    st->main_loops  = 12345U;
    st->heartbeat   = 67890U;
    st->display_id  = 0x00934108U;   /* raw 0xD3 read: [0x00,0x93,0x41,0x08] */
    st->madctl      = 0x20000000U;   /* raw 0x0B read: [0x20,...]            */
    st->pa_idr      = 0x0010U;
    for (uint8_t i = 0U; i < DIAG_ADC_CHANNELS; i++) {
        st->adc[i] = (uint16_t)(0x100U + 0x11U * i);
    }
    st->sw_pos[0] = 1U;
    st->sw_pos[1] = 0U;
    st->sw_pos[2] = 2U;
    st->combo     = 7U;
    st->coupling  = 0U;
    st->init_errors = 0U;
}

#endif /* DIAG_FIXTURE_H */
