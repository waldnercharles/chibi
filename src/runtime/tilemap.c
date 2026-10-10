#include "runtime/tilemap.h"

#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void tilemap_grid_free(tilemap_grid *grid)
{
    assert(grid);
    free(grid->frames);
    free(grid->cells);
    *grid = (tilemap_grid){ 0 };
}

static bool parse_cells(tilemap_grid *grid)
{
    if (grid->columns <= 0 || grid->rows <= 0 || grid->frame_width <= 0 ||
        grid->frame_height <= 0 || grid->columns > INT_MAX / grid->rows ||
        (size_t)grid->columns * (size_t)grid->rows > SIZE_MAX / sizeof(int)) {
        grid->error = "Tilemap dimensions must be positive and fit the cell "
                      "index range";
        return false;
    }
    int count = grid->columns * grid->rows;
    const char *text = grid->cells;
    /* Every cell needs at least one character plus separating whitespace. */
    if ((size_t)count > (strlen(text) + 1) / 2) {
        grid->error = "Tilemap cell count does not match its dimensions";
        return false;
    }
    int *frames = xmalloc((size_t)count * sizeof *frames);
    for (int i = 0; i < count; i++) {
        while (isspace((unsigned char)*text)) {
            text++;
        }
        errno = 0;
        char *end;
        long frame = strtol(text, &end, 10);
        if (end == text || errno == ERANGE || frame < -1 || frame > INT_MAX ||
            (*end && !isspace((unsigned char)*end))) {
            free(frames);
            grid->error = "Tilemap cells must be decimal frame numbers or -1";
            return false;
        }
        frames[i] = (int)frame;
        text = end;
    }
    while (isspace((unsigned char)*text)) {
        text++;
    }
    if (*text) {
        free(frames);
        grid->error = "Tilemap cell count does not match its dimensions";
        return false;
    }
    grid->frames = frames;
    return true;
}

bool tilemap_grid_update(tilemap_grid *grid, const char *cells, int columns, int rows, int frame_width, int frame_height)
{
    assert(grid);
    const char *text = cells ? cells : "";
    if (grid->initialized && grid->columns == columns && grid->rows == rows &&
        grid->frame_width == frame_width &&
        grid->frame_height == frame_height && !strcmp(grid->cells, text)) {
        return grid->frames != NULL;
    }
    /* Copy first, so callers may use the retained cells as the new source. */
    char *copy = xstrdup(text);
    tilemap_grid_free(grid);
    *grid = (tilemap_grid){ .cells = copy,
                            .columns = columns,
                            .rows = rows,
                            .frame_width = frame_width,
                            .frame_height = frame_height,
                            .initialized = true };
    return parse_cells(grid);
}

int tilemap_grid_frame(const tilemap_grid *grid, int column, int row)
{
    if (!grid->frames || column < 0 || row < 0 || column >= grid->columns ||
        row >= grid->rows) {
        return -1;
    }
    return grid->frames[row * grid->columns + column];
}

int tilemap_grid_point(const tilemap_grid *grid, CF_V2 translation, float radians, CF_V2 world_point)
{
    if (!grid->frames || !isfinite(radians)) {
        return -1;
    }
    double c = cos((double)radians), s = sin((double)radians);
    double dx = (double)world_point.x - translation.x;
    double dy = (double)world_point.y - translation.y;
    double x = (c * dx + s * dy) / grid->frame_width;
    double y = (-s * dx + c * dy) / grid->frame_height;
    if (!(x >= 0 && x < grid->columns && y >= 0 && y < grid->rows)) {
        return -1;
    }
    return tilemap_grid_frame(grid, (int)x, (int)y);
}

int tilemap_grid_overlap_count(const tilemap_grid *grid, CF_V2 translation, float radians, CF_Aabb box)
{
    if (!grid->frames || !isfinite(radians) || !isfinite(translation.x) ||
        !isfinite(translation.y) || !isfinite(box.min.x) ||
        !isfinite(box.min.y) || !isfinite(box.max.x) || !isfinite(box.max.y) ||
        !(box.max.x > box.min.x && box.max.y > box.min.y)) {
        return 0;
    }
    double c = cos((double)radians), s = sin((double)radians);
    double ac = fabs(c), as = fabs(s);
    double hx = ((double)box.max.x - box.min.x) * .5;
    double hy = ((double)box.max.y - box.min.y) * .5;
    double dx = ((double)box.max.x + box.min.x) * .5 - translation.x;
    double dy = ((double)box.max.y + box.min.y) * .5 - translation.y;
    double x = c * dx + s * dy, y = -s * dx + c * dy;
    double rx = ac * hx + as * hy, ry = as * hx + ac * hy;
    double w = grid->frame_width, h = grid->frame_height;
    /* Clamp before converting to integer: arbitrary world coordinates may be huge. */
    int left = (int)fmax(0, fmin(grid->columns, floor((x - rx) / w)));
    int right = (int)fmax(0, fmin(grid->columns, ceil((x + rx) / w)));
    int top = (int)fmax(0, fmin(grid->rows, floor((y - ry) / h)));
    int bottom = (int)fmax(0, fmin(grid->rows, ceil((y + ry) / h)));
    int count = 0;
    for (int row = top; row < bottom; row++) {
        for (int column = left; column < right; column++) {
            if (tilemap_grid_frame(grid, column, row) < 0) {
                continue;
            }
            double lx = x - (column + .5) * w;
            double ly = y - (row + .5) * h;
            /* The two local cell axes and two world query axes separate rotated
             * rectangles exactly. Equality is touching, not an overlap. */
            if (fabs(lx) >= w * .5 + rx || fabs(ly) >= h * .5 + ry ||
                fabs(c * lx - s * ly) >= hx + ac * w * .5 + as * h * .5 ||
                fabs(s * lx + c * ly) >= hy + as * w * .5 + ac * h * .5) {
                continue;
            }
            count++;
        }
    }
    return count;
}

bool tilemap_bounds(
    int columns,
    int rows,
    int frame_width,
    int frame_height,
    CF_V2 translation,
    float radians,
    CF_V2 *min,
    CF_V2 *max
)
{
    assert(min && max);
    if (columns <= 0 || rows <= 0 || frame_width <= 0 || frame_height <= 0 ||
        columns > INT_MAX / rows || !isfinite(translation.x) ||
        !isfinite(translation.y) || !isfinite(radians)) {
        return false;
    }
    double w = (double)columns * frame_width;
    double h = (double)rows * frame_height;
    double c = cos((double)radians), s = sin((double)radians);
    double x = translation.x + c * w * .5 - s * h * .5;
    double y = translation.y + s * w * .5 + c * h * .5;
    double rx = fabs(c) * w * .5 + fabs(s) * h * .5;
    double ry = fabs(s) * w * .5 + fabs(c) * h * .5;
    *min = (CF_V2){ (float)(x - rx), (float)(y - ry) };
    *max = (CF_V2){ (float)(x + rx), (float)(y + ry) };
    return true;
}
