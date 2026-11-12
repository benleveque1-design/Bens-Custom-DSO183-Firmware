#include "link_host.h"

#include <stddef.h>

/* Dispatch one already-parsed command onto the application hooks.
 *
 * The hooks are allowed to be NULL: a build that does not implement, say,
 * streaming still answers, it just reports BAD_ARG instead of lying with OK.
 *
 * Range policy is enforced HERE, not by the hooks: the wire carries a 16-bit
 * argument, while the engine takes narrower types, so a blind cast would
 * silently wrap (SET_TIMEBASE 261 would become index 5 and be applied).  An
 * out-of-range argument is answered with BAD_ARG and no hook is called.
 * Unknown opcodes keep their own UNKNOWN status. */

uint8_t link_host_exec(const struct link_cmd *cmd, const struct link_ops *ops,
                       uint8_t *status, uint16_t *value)
{
    uint8_t ok;

    if ((cmd == NULL) || (ops == NULL) || (status == NULL) || (value == NULL)) {
        return 0U;
    }

    *value = 0U;

    if ((cmd->op < (uint8_t)LINK_OP_COUNT)
        && (link_cmd_arg_valid(cmd->op, cmd->arg) == 0U)) {
        *status = LINK_ST_BAD_ARG;
        return 1U;
    }

    switch (cmd->op) {
    case LINK_OP_NOP:
        ok = 1U;
        break;

    case LINK_OP_SET_TIMEBASE:
        ok = (ops->set_timebase != NULL)
             ? ops->set_timebase((uint8_t)cmd->arg) : 0U;
        break;

    case LINK_OP_SET_TRIGGER:
        ok = (ops->set_trigger != NULL)
             ? ops->set_trigger(cmd->arg) : 0U;
        break;

    case LINK_OP_SET_SPAN:
        ok = (ops->set_span != NULL)
             ? ops->set_span(cmd->arg) : 0U;
        break;

    case LINK_OP_RUN:
        ok = (ops->set_run != NULL) ? ops->set_run(1U) : 0U;
            if (ok != 0U) { *value = 1U; }   /* echo: running */
        break;

    case LINK_OP_STOP:
        ok = (ops->set_run != NULL) ? ops->set_run(0U) : 0U;
        break;

    case LINK_OP_STREAM:
        ok = (ops->set_stream != NULL)
             ? ops->set_stream((cmd->arg != 0U) ? 1U : 0U) : 0U;
        break;

    case LINK_OP_GET_STATUS:
        *status = LINK_ST_OK;
        *value  = (ops->get_status != NULL) ? ops->get_status() : 0U;
        return 1U;

    case LINK_OP_SET_TRIG_MODE:
        ok = (ops->set_trigger_mode != NULL)
             ? ops->set_trigger_mode((uint8_t)cmd->arg) : 0U;
        if (ok != 0U) { *value = (uint16_t)(cmd->arg & 0xFFU); }
        break;

    case LINK_OP_GET_VALUE:
        *status = LINK_ST_OK;
        *value  = (ops->get_value != NULL) ? ops->get_value((uint8_t)cmd->arg) : 0U;
        return 1U;

    case LINK_OP_SET_TRIG_SLOPE:
        ok = (ops->set_trigger_slope != NULL)
             ? ops->set_trigger_slope((uint8_t)cmd->arg) : 0U;
        if (ok != 0U) { *value = (uint16_t)(cmd->arg & 0xFFU); }
        break;

    case LINK_OP_CAL_SET_OFFSET_MV:
        ok = (ops->set_cal_offset_mv != NULL)
             ? ops->set_cal_offset_mv((int16_t)cmd->arg) : 0U;
        if (ok != 0U) { *value = cmd->arg; }
        break;

    case LINK_OP_CAL_SET_GAIN_X1000:
        ok = (ops->set_cal_gain_x1000 != NULL)
             ? ops->set_cal_gain_x1000(cmd->arg) : 0U;
        if (ok != 0U) { *value = cmd->arg; }
        break;

    case LINK_OP_CAL_SAVE:
        ok = (ops->cal_save != NULL) ? ops->cal_save() : 0U;
        break;

    case LINK_OP_CAL_RESET:
        ok = (ops->cal_reset != NULL) ? ops->cal_reset() : 0U;
        break;

    case LINK_OP_WAVE_SAVE:
        ok = (ops->wave_save != NULL) ? ops->wave_save((uint8_t)cmd->arg) : 0U;
        if (ok != 0U) { *value = (uint16_t)(cmd->arg & 1U); }
        break;

    case LINK_OP_WAVE_RECALL:
        ok = (ops->wave_recall != NULL) ? ops->wave_recall((uint8_t)cmd->arg) : 0U;
        if (ok != 0U) { *value = (uint16_t)(cmd->arg & 1U); }
        break;

    case LINK_OP_SET_AVG:
        ok = (ops->set_avg != NULL) ? ops->set_avg((uint8_t)cmd->arg) : 0U;
        if (ok != 0U) { *value = (uint16_t)(cmd->arg & 0xFFU); }
        break;

    default:
        *status = LINK_ST_UNKNOWN;
        return 1U;
    }

    if ((cmd->op == LINK_OP_STREAM) && (ok != 0U)) {
        *value = (cmd->arg != 0U) ? 1U : 0U;   /* echo new state */
    }

    *status = (ok != 0U) ? LINK_ST_OK : LINK_ST_BAD_ARG;
    return 1U;
}
