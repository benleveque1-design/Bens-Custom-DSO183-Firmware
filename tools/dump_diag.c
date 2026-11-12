/* Host helper: print the diagnostic rows exactly as the firmware builds them.
 *
 * Built by `make dump_diag` and consumed by tools/render_diag.py, which
 * renders the same rows with the same font and geometry the ILI9341 driver
 * uses.  The state values are fixed here so the preview is reproducible.
 */
#include "diag.h"
#include "diag_fixture.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    static char rows[DIAG_ROWS][DIAG_COLS];
    struct diag_state st;

    diag_fixture(&st);

    diag_rows(rows, &st);
    for (uint8_t r = 0U; r < DIAG_ROWS; r++) {
        printf("%u|%s\n", (unsigned)r, rows[r]);
    }
    return 0;
}
