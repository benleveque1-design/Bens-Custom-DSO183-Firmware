/* Host-command dispatch - the device side of PC-link protocol v1.
 *
 * Kept free of HAL and of global state so the policy is host-testable: the
 * caller supplies function pointers that actually touch the acquisition
 * engine, and this module decides which opcode maps to which action and what
 * status comes back.  Keeping the switch here means the wire protocol and the
 * device policy cannot drift apart silently.
 */
#ifndef LINK_HOST_H
#define LINK_HOST_H

#include <stdint.h>

#include "link_cmd.h"

struct link_ops {
    uint8_t (*set_timebase)(uint8_t index);   /* 1 = applied              */
    uint8_t (*set_trigger)(uint16_t level);   /* 1 = applied              */
    uint8_t (*set_span)(uint16_t span);       /* 1 = applied              */
    uint8_t (*set_run)(uint8_t run);          /* 1 = run, 0 = hold        */
    uint8_t (*set_stream)(uint8_t on);        /* 1 = frames on            */
    uint16_t (*get_status)(void);             /* packed state for GET_STATUS */
    uint8_t (*set_trigger_mode)(uint8_t mode);/* 1 = applied              */
    uint16_t (*get_value)(uint8_t subindex);  /* GET_VALUE payload        */
    uint8_t (*set_trigger_slope)(uint8_t slope); /* 0 rise,1 fall,2 auto  */
    uint8_t (*set_cal_offset_mv)(int16_t mv); /* 1 = applied              */
    uint8_t (*set_cal_gain_x1000)(uint16_t g);/* 1 = applied              */
    uint8_t (*cal_save)(void);                /* 1 = persisted            */
    uint8_t (*cal_reset)(void);               /* 1 = reset                */
    uint8_t (*wave_save)(uint8_t slot);       /* 1 = saved                */
    uint8_t (*wave_recall)(uint8_t slot);     /* 1 = recalled             */
    uint8_t (*set_avg)(uint8_t n);            /* 1,2,4,8                  */
};

/* Execute one decoded command.  Returns 1 when a reply must be sent (which is
 * always the case for a well-formed command), 0 only for NULL arguments.
 * `*status` receives LINK_ST_* and `*value` the reply payload. */
uint8_t link_host_exec(const struct link_cmd *cmd, const struct link_ops *ops,
                       uint8_t *status, uint16_t *value);

#endif /* LINK_HOST_H */
