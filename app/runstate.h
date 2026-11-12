/* Run/stream state - one owner for the two flags the panel, the PC link and
 * the persistence layer all need to agree on.
 *
 * Both used to be loose statics in main.c, which let the menu persist a stale
 * copy of a value that the PC link had since changed (RUN/STOP over the link
 * was silently reverted by the next menu save).  Keeping them here means the
 * menu and the link edit the same variable and the save path always reads the
 * current one.
 *
 * No HAL, no floating point: host-testable.
 */
#ifndef RUNSTATE_H
#define RUNSTATE_H

#include <stdint.h>

/* Running with streaming off - the state a reset must come up in, whatever
 * the previous build left behind. */
void runstate_init(void);

uint8_t runstate_run(void);
void    runstate_run_set(uint8_t on);

uint8_t runstate_stream(void);
void    runstate_stream_set(uint8_t on);

#endif /* RUNSTATE_H */