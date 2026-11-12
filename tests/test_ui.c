/* Host unit tests for the UI geometry (app/ui.c). */
#include "ui.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_constants(void)
{
    CHECK(UI_W == 320);
    CHECK(UI_H_PIX == 240);
    CHECK(UI_GRID_W == 320);
    CHECK(UI_GRID_H == 184);
}

static void test_grid_x(void)
{
    CHECK(ui_grid_x(0u) == 0u);
    CHECK(ui_grid_x(1u) == 32u);
    CHECK(ui_grid_x(9u) == 288u);
    /* out-of-range clamps to the last division */
    CHECK(ui_grid_x(UI_GRID_DIVS_X) == 288u);
    CHECK(ui_grid_x(255u) == 288u);
}

static void test_grid_y(void)
{
    CHECK(ui_grid_y(0u) == 16u);
    CHECK(ui_grid_y(1u) == 39u);
    CHECK(ui_grid_y(7u) == 177u);
    CHECK(ui_grid_y(UI_GRID_DIVS_Y) == 177u);
    CHECK(ui_grid_y(255u) == 177u);
}

static void test_in_grid(void)
{
    /* corners are inside */
    CHECK(ui_in_grid(0u, 16u) == 1u);
    CHECK(ui_in_grid(319u, 199u) == 1u);

    /* just outside on every edge */
    CHECK(ui_in_grid(0u, 15u) == 0u);
    CHECK(ui_in_grid(0u, 200u) == 0u);
    CHECK(ui_in_grid(320u, 100u) == 0u);
    CHECK(ui_in_grid(160u, 0u) == 0u);

    /* middle */
    CHECK(ui_in_grid(160u, 100u) == 1u);
}

int main(void)
{
    test_constants();
    test_grid_x();
    test_grid_y();
    test_in_grid();

    if (failures == 0) {
        printf("ui: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
