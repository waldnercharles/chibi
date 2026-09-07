#include "scene_navigation.h"

#include "../core/array.h"
#include "../core/xalloc.h"

#include <stdlib.h>
#include <string.h>

enum
{
    NAVIGATION_LIMIT = 128
};

struct scene_navigation
{
    scene_location *locations;
    int cursor;
};

static bool same_text(const char *a, const char *b)
{
    return a && b ? strcmp(a, b) == 0 : a == b;
}

static bool same_location(const scene_location *a, const scene_location *b)
{
    if (a->kind != b->kind || a->tab != b->tab ||
        ecs_entity_to_u64(a->entity) != ecs_entity_to_u64(b->entity) ||
        a->graph.graph_id != b->graph.graph_id ||
        a->graph.node_count != b->graph.node_count ||
        !same_text(a->document, b->document) ||
        !same_text(a->scene_path, b->scene_path) ||
        !same_text(a->system_path, b->system_path) ||
        !same_text(a->prototype, b->prototype) || !same_text(a->content, b->content) ||
        !same_text(a->attachment, b->attachment)) {
        return false;
    }
    for (int i = 0; i < a->graph.node_count; i++) {
        if (a->graph.nodes[i] != b->graph.nodes[i]) {
            return false;
        }
    }
    return true;
}

static void free_location(scene_location *location)
{
    free((char *)location->document);
    free((char *)location->scene_path);
    free((char *)location->system_path);
    free((char *)location->prototype);
    free((char *)location->content);
    free((char *)location->attachment);
    free(location->graph.nodes);
}

static scene_location copy_location(const scene_location *source)
{
    scene_location copy = *source;
    copy.document = source->document ? xstrdup(source->document) : NULL;
    copy.scene_path = source->scene_path ? xstrdup(source->scene_path) : NULL;
    copy.system_path = source->system_path ? xstrdup(source->system_path) : NULL;
    copy.prototype = source->prototype ? xstrdup(source->prototype) : NULL;
    copy.content = source->content ? xstrdup(source->content) : NULL;
    copy.attachment = source->attachment ? xstrdup(source->attachment) : NULL;
    copy.graph.nodes = NULL;
    if (source->graph.node_count) {
        size_t size = (size_t)source->graph.node_count * sizeof *copy.graph.nodes;
        copy.graph.nodes = xmalloc(size);
        memcpy(copy.graph.nodes, source->graph.nodes, size);
    }
    return copy;
}

scene_navigation *scene_navigation_new()
{
    scene_navigation *navigation = xcalloc(1, sizeof *navigation);
    navigation->cursor = -1;
    return navigation;
}

void scene_navigation_free(scene_navigation *navigation)
{
    for (int i = 0; i < acount(navigation->locations); i++) {
        free_location(&navigation->locations[i]);
    }
    afree(navigation->locations);
    free(navigation);
}

void scene_navigation_record(scene_navigation *navigation, const scene_location *location)
{
    if (navigation->cursor >= 0 &&
        same_location(&navigation->locations[navigation->cursor], location)) {
        scene_location copy = copy_location(location);
        free_location(&navigation->locations[navigation->cursor]);
        navigation->locations[navigation->cursor] = copy;
        return;
    }
    for (int i = navigation->cursor + 1; i < acount(navigation->locations); i++) {
        free_location(&navigation->locations[i]);
    }
    asetlen(navigation->locations, navigation->cursor + 1);
    apush(navigation->locations, copy_location(location));
    navigation->cursor++;
    if (acount(navigation->locations) > NAVIGATION_LIMIT) {
        free_location(&navigation->locations[0]);
        adel(navigation->locations, 0);
        navigation->cursor--;
    }
}

bool scene_navigation_can_move(const scene_navigation *navigation, bool forward)
{
    return forward ? navigation->cursor + 1 < acount(navigation->locations)
                   : navigation->cursor > 0;
}

const scene_location *scene_navigation_step(scene_navigation *navigation, bool forward)
{
    if (!scene_navigation_can_move(navigation, forward)) {
        return NULL;
    }
    navigation->cursor += forward ? 1 : -1;
    return &navigation->locations[navigation->cursor];
}

void scene_navigation_discard_current(scene_navigation *navigation, bool forward)
{
    free_location(&navigation->locations[navigation->cursor]);
    adel(navigation->locations, navigation->cursor);
    if (forward) {
        navigation->cursor--;
    }
}
