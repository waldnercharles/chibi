#ifndef CHIBI_TEST_RENDER_MATERIAL_BORDERS_H
#define CHIBI_TEST_RENDER_MATERIAL_BORDERS_H

#include "runtime/material_mask.h"

#include <stdbool.h>

/* Material border masks the CPU and GPU border cases share. */

enum
{
    /* Each fixture is TEST_BORDER_SIDE cells square. */
    TEST_BORDER_SIDE = 10,
    TEST_BORDER_FIXTURES = 7
};

/* Fills the cells of `fixture`: full, a notched triangle, a hollow square,
 * scattered blocks, a disc, an ellipse and a diamond. */
void test_border_fixture(int fixture, unsigned char *cells);
/* Whether cell (x, y) of `mask` is filled and has an empty cell or the
 * outside within `reach` along a row or column, found by scanning every
 * candidate rather than the way the renderer builds runs. */
bool test_border_cell(material_occupancy_view mask, int reach, int x, int y);

#endif
