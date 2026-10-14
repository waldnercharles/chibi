#include "authoring/resource_preview_data.h"

#include "assets/ui_source.h"
#include "authoring/css_values.h"
#include "graph/authoring/layout.h"
#include "runtime/color_value.h"
#include "support/array.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool extension_is(const char *actual, const char *expected)
{
    while (*actual && *expected) {
        if (tolower((unsigned char)*actual++) != *expected++) {
            return false;
        }
    }
    return *actual == *expected;
}
resource_preview_route_kind resource_preview_route(const char *kind, const char *path)
{
    if (!kind || !path) {
        return RESOURCE_PREVIEW_NONE;
    }
    if (!strcmp(kind, "scene") || !strcmp(kind, "prototype")) {
        return RESOURCE_PREVIEW_SCENE;
    }
    if (!strcmp(kind, "material") || !strcmp(kind, "shape") ||
        !strcmp(kind, "model") || !strcmp(kind, "field")) {
        return RESOURCE_PREVIEW_VISUAL;
    }
    if (!strcmp(kind, "behavior") || !strcmp(kind, "system") ||
        !strcmp(kind, "library") || !strcmp(kind, "shader-library") ||
        !strcmp(kind, "event")) {
        return RESOURCE_PREVIEW_GRAPH;
    }
    const char *extension = strrchr(path, '.');
    if (!extension) {
        return RESOURCE_PREVIEW_NONE;
    }
    if (!strcmp(kind, "ui") && extension_is(extension, ".rml")) {
        return RESOURCE_PREVIEW_UI;
    }
    if ((!strcmp(kind, "stylesheet") || !strcmp(kind, "ui")) &&
        extension_is(extension, ".rcss")) {
        return RESOURCE_PREVIEW_STYLESHEET;
    }
    if (!strcmp(kind, "image") &&
        (extension_is(extension, ".png") || extension_is(extension, ".jpg") ||
         extension_is(extension, ".jpeg"))) {
        return RESOURCE_PREVIEW_RASTER;
    }
    return RESOURCE_PREVIEW_NONE;
}
typedef struct
{
    int start, end;
} range;
static int order(const void *a, const void *b)
{
    const range *x = a, *y = b;
    return (x->start > y->start) - (x->start < y->start);
}
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
bool resource_preview_palette(const char *text, unsigned char colors[64][4], int *count, char *error, size_t size)
{
    *count = 0;
    if (!text || strnlen(text, 1024 * 1024 + 1) > 1024 * 1024) {
        return fail(error, size, "Stylesheet thumbnail source exceeds1MiB");
    }
    ui_source *source = ui_source_parse(text, UI_SOURCE_RCSS);
    if (acount(source->diagnostics)) {
        ui_source_free(source);
        return fail(error, size, "Stylesheet thumbnail source has syntax errors");
    }
    range ranges[4096];
    int length = 0;
    for (int i = 0; i < acount(source->rules); i++) {
        for (int j = 0; j < acount(source->rules[i].declarations); j++) {
            if (length == 4096) {
                ui_source_free(source);
                return fail(error, size, "Stylesheet thumbnail exceeds4096declarations");
            }
            ui_source_declaration d = source->rules[i].declarations[j];
            ranges[length++] = (range){ d.value_start, d.value_end };
        }
    }
    qsort(ranges, (size_t)length, sizeof *ranges, order);
    int declaration = 0;
    for (int i = 0; i < acount(source->spans) && *count < 64; i++) {
        ui_source_span span = source->spans[i];
        while (declaration < length && ranges[declaration].end <= span.start) {
            declaration++;
        }
        int bytes = span.end - span.start;
        if (declaration == length) {
            break;
        }
        if (span.kind != UI_SOURCE_NUMBER || text[span.start] != '#' ||
            bytes < 4 || bytes > 9 || span.start < ranges[declaration].start ||
            span.end > ranges[declaration].end) {
            continue;
        }
        char literal[10] = { 0 };
        memcpy(literal, text + span.start, (size_t)bytes);
        css_value value = css_value_query("color", literal);
        char canonical[10], ignored[64];
        if (!value.simple || !value.color ||
            !color_value_to_hex(value.rgba, canonical, ignored, sizeof ignored)) {
            continue;
        }
        unsigned char rgba[4];
        for (int channel = 0; channel < 4; channel++) {
            char pair[3] = { canonical[1 + channel * 2], canonical[2 + channel * 2], 0 };
            rgba[channel] = (unsigned char)strtoul(pair, NULL, 16);
        }
        bool duplicate = false;
        for (int c = 0; c < *count; c++) {
            if (!memcmp(colors[c], rgba, 4)) {
                duplicate = true;
            }
        }
        if (!duplicate) {
            memcpy(colors[(*count)++], rgba, 4);
        }
    }
    ui_source_free(source);
    if (!*count) {
        return fail(error, size, "Stylesheet has no literal hex color swatches");
    }
    if (size) {
        error[0] = 0;
    }
    return true;
}

static int graph_index(const graph *g, uint64_t id)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i]->id == id) {
            return i;
        }
    }
    return -1;
}
static void pixel(unsigned char *rgba, int width, int height, int x, int y, const unsigned char color[4])
{
    if (x >= 0 && x < width && y >= 0 && y < height) {
        memcpy(rgba + ((size_t)y * width + x) * 4, color, 4);
    }
}
bool resource_preview_graph(const graph *g, int width, int height, unsigned char *rgba, char *error, size_t size)
{
    if (!g || !rgba || width < 1 || height < 1 || width > 256 || height > 256 ||
        acount(g->nodes) < 1 || acount(g->nodes) > 512 || acount(g->edges) > 2048) {
        return fail(error, size, "Graph thumbnail requires1..512nodes, <=2048wires and1..256pixel sides");
    }
    arrange_box boxes[512];
    float left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        boxes[i] = graph_layout_box(g, n);
        if (!isfinite(n->x) || !isfinite(n->y) || !isfinite(boxes[i].width) ||
            !isfinite(boxes[i].height) || boxes[i].width <= 0 || boxes[i].height <= 0) {
            return fail(error, size, "Graph thumbnail has invalid geometry");
        }
        left = fminf(left, n->x);
        top = fminf(top, n->y);
        right = fmaxf(right, n->x + boxes[i].width);
        bottom = fmaxf(bottom, n->y + boxes[i].height);
    }
    double scale = fmin((width - 8.0) / (right - left), (height - 8.0) / (bottom - top));
    if (width <= 8 || height <= 8 || !isfinite(scale) || scale <= 0) {
        return fail(error, size, "Graph thumbnail has no finite visible extent");
    }
    double origin_x = (width - (right - left) * scale) * 0.5,
           origin_y = (height - (bottom - top) * scale) * 0.5;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            memcpy(rgba + ((size_t)y * width + x) * 4, (unsigned char[]){ 17, 24, 35, 255 }, 4);
        }
    }
    for (int i = 0; i < acount(g->edges); i++) {
        edge e = g->edges[i];
        int from = graph_index(g, e.from_id), to = graph_index(g, e.to_id);
        if (from < 0 || to < 0) {
            continue;
        }
        double x1 = origin_x + (g->nodes[from]->x + boxes[from].width - left) * scale,
               y1 = origin_y +
                    (g->nodes[from]->y + boxes[from].height * 0.5 - top) * scale;
        double x2 = origin_x + (g->nodes[to]->x - left) * scale,
               y2 = origin_y + (g->nodes[to]->y + boxes[to].height * 0.5 - top) * scale;
        int steps = (int)ceil(fmax(fabs(x2 - x1), fabs(y2 - y1)));
        const unsigned char *color = e.kind == GRAPH_EDGE_EXEC
                                       ? (unsigned char[]){ 190, 196, 204, 255 }
                                       : (unsigned char[]){ 64, 162, 178, 255 };
        for (int step = 0; step <= steps; step++) {
            double t = steps ? (double)step / steps : 0;
            pixel(
                rgba,
                width,
                height,
                (int)lround(x1 + (x2 - x1) * t),
                (int)lround(y1 + (y2 - y1) * t),
                color
            );
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        int x1 = (int)floor(origin_x + (n->x - left) * scale),
            y1 = (int)floor(origin_y + (n->y - top) * scale);
        int x2 = (int)ceil(origin_x + (n->x + boxes[i].width - left) * scale),
            y2 = (int)ceil(origin_y + (n->y + boxes[i].height - top) * scale);
        for (int y = y1; y < y2; y++) {
            for (int x = x1; x < x2; x++) {
                pixel(rgba, width, height, x, y, (unsigned char[]){ 65, 83, 104, 255 });
            }
        }
        for (int x = x1; x < x2; x++) {
            pixel(rgba, width, height, x, y1, (unsigned char[]){ 105, 143, 161, 255 });
        }
    }
    if (size) {
        error[0] = 0;
    }
    return true;
}
