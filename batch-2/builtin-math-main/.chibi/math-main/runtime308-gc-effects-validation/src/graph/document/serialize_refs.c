#include "graph/document/serialize_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void serialize_pin_key(const node *n, int output, int index, char *out, size_t size)
{
    const pin_spec *pin = output ? &n->output_pins[index] : &n->input_pins[index];
    for (int i = 0; output && pin->field_source && i < n->output_count; i++) {
        if (i != index && n->output_pins[i].id == pin->field_source) {
            char source[SERIALIZE_KEY_SIZE];
            serialize_pin_key(n, 1, i, source, sizeof source);
            snprintf(out, size, "%s.%s", source, pin->field_name ? pin->field_name : "");
            return;
        }
    }
    snprintf(out, size, "%s", pin->name ? pin->name : "");
}

int serialize_pin_by_key(const node *n, int output, const char *key)
{
    int count = output ? n->output_count : n->input_count;
    int found = -1;
    for (int i = 0; i < count; i++) {
        char candidate[SERIALIZE_KEY_SIZE];
        serialize_pin_key(n, output, i, candidate, sizeof candidate);
        if (!strcmp(candidate, key)) {
            if (found >= 0) {
                return -1;
            }
            found = i;
        }
    }
    return found;
}

bool serialize_pin_ref(const node *n, int output, int index, char *out, size_t size)
{
    char key[SERIALIZE_KEY_SIZE];
    serialize_pin_key(n, output, index, key, sizeof key);
    if (serialize_pin_by_key(n, output, key) != index) {
        return false;
    }
    snprintf(out, size, "%llu.%s", (unsigned long long)n->id, key);
    return true;
}

bool serialize_resolve_ref(const graph *g, const char *ref, int output, node **n, int *index)
{
    char *end = NULL;
    unsigned long long id = strtoull(ref, &end, 10);
    if (end == ref || *end != '.') {
        return false;
    }
    node *target = graph_node_by_id(g, id);
    int at = target ? serialize_pin_by_key(target, output, end + 1) : -1;
    if (at < 0) {
        return false;
    }
    *n = target;
    *index = at;
    return true;
}
