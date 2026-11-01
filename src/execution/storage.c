#include "execution/storage.h"

#include "execution/definition.h"
#include "support/array.h"

#include <stdint.h>

static size_t add(size_t total, size_t bytes)
{
    return bytes > SIZE_MAX - total ? SIZE_MAX : total + bytes;
}
static size_t vector(const void *data, size_t item)
{
    if (!data) {
        return 0;
    }
    size_t capacity = (size_t)acap(data);
    if (capacity > (SIZE_MAX - sizeof(dyna_hdr)) / item) {
        return SIZE_MAX;
    }
    return sizeof(dyna_hdr) + capacity * item;
}
static size_t own_bytes(const behavior_function *f)
{
    size_t bytes = add(sizeof *f, arena_storage_bytes(&f->owned));
#define COUNT(field) bytes = add(bytes, vector(f->field, sizeof *f->field))
    COUNT(argument_defaults);
    COUNT(result_defaults);
    COUNT(variable_defaults);
    COUNT(argument_kinds);
    COUNT(result_kinds);
    COUNT(slot_kinds);
    COUNT(command_argument_kinds);
    COUNT(command_result_kinds);
    COUNT(suspensions);
    COUNT(value_roots);
    COUNT(points);
    COUNT(source_points);
    COUNT(source_watches);
    COUNT(watches);
    COUNT(edges);
    COUNT(commands);
    COUNT(command_arguments);
    COUNT(command_results);
    COUNT(queries);
    COUNT(host_calls);
    COUNT(calls);
    COUNT(definitions);
    COUNT(data_values);
    COUNT(instructions);
    COUNT(slots);
    COUNT(locals);
    COUNT(parameters);
    COUNT(frames);
    COUNT(sources);
    COUNT(stacks);
    COUNT(results);
    COUNT(runtime_instructions);
    COUNT(runtime_body);
    COUNT(component_writes);
    COUNT(components);
    COUNT(state_selections);
#undef COUNT
    if (f->integer_reports) {
        bytes = add(bytes, behavior_integer_report_bytes(f->integer_report_sites));
    }
    if (f->runtime_map) {
        bytes = add(bytes, ((size_t)acount(f->instructions) + 1) * sizeof *f->runtime_map);
    }
    return bytes;
}
size_t behavior_function_storage_bytes(const behavior_function *root)
{
    if (!root) {
        return 0;
    }
    const behavior_function **pending = NULL;
    apush(pending, root);
    size_t bytes = 0;
    while (acount(pending)) {
        const behavior_function *f = alast(pending);
        asetlen(pending, acount(pending) - 1);
        bytes = add(bytes, own_bytes(f));
        if (bytes == SIZE_MAX) {
            break;
        }
        for (int i = 0; i < acount(f->definitions); i++) {
            apush(pending, f->definitions[i]);
        }
        if (f->fast) {
            apush(pending, f->fast);
        }
    }
    afree(pending);
    return bytes;
}
