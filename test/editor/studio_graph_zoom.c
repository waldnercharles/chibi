#include "editor/studio/studio_graph_internal.h"
#include "support/array.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <math.h>
#include <string.h>

void test_studio_graph_zoom(studio *s, node *n)
{
    assert(s->graph.kit && n && n->input_count > 0 && n->output_count > 0);
    float old_zoom = s->graph.zoom;
    float baseline[4] = { 0 }, input[2] = { 0 }, output[2] = { 0 };
    bool stable = true;
    const float zooms[] = { 0.72f, 0.83f, 1.0f, 1.17f, 1.4f, 1.8f };
    for (int i = 0; i < (int)(sizeof zooms / sizeof *zooms); i++) {
        s->graph.zoom = zooms[i];
        asetlen(s->graph.layouts, 0);
        float box[4], input_at[2], output_at[2];
        sg_node_box(s, n, box);
        sg_socket_at(s, n, ED_PIN_INPUT, 0, &input_at[0], &input_at[1]);
        sg_socket_at(s, n, ED_PIN_OUTPUT, 0, &output_at[0], &output_at[1]);
        if (i == 0) {
            memcpy(baseline, box, sizeof box);
            memcpy(input, input_at, sizeof input_at);
            memcpy(output, output_at, sizeof output_at);
        }
        for (int axis = 0; axis < 4; axis++) {
            stable &= fabsf(box[axis] - baseline[axis]) < 0.001f;
        }
        for (int axis = 0; axis < 2; axis++) {
            stable &= fabsf(input_at[axis] - input[axis]) < 0.001f &&
                      fabsf(output_at[axis] - output[axis]) < 0.001f;
        }
    }
    s->graph.zoom = old_zoom;
    asetlen(s->graph.layouts, 0);
    test_check(stable, "node bounds and wire anchors stay fixed in graph units while zoom changes");
}
