#include "language/semantic_internal.h"
#include "support/array.h"
#include "types/record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *own(analyzer *a, const char *text)
{
    size_t bytes = strlen(text) + 1;
    char *copy = arena_alloc(&a->out->constants, bytes);
    memcpy(copy, text, bytes);
    return copy;
}
void script_analyzer_record_types(analyzer *a, TSNode body)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (!script_analyzer_is(node, "record_declaration")) {
            continue;
        }
        if (a->out->kind != SCRIPT_PROGRAM_LIBRARY || a->function >= 0) {
            script_analyzer_diagnostic(a, node, "Records must be declared in a library");
            continue;
        }
        int symbol = script_analyzer_symbol_new(
            a,
            script_analyzer_field(node, "name"),
            SCRIPT_TYPE,
            (script_type){ .storage = PIN_STRUCT },
            false
        );
        script_symbol *s = &a->out->symbols[symbol];
        script_range name_range = script_analyzer_range(a, script_analyzer_field(node, "name"));
        if (s->kind != SCRIPT_TYPE || s->declaration.document != name_range.document ||
            s->declaration.start != name_range.start) {
            continue;
        }
        s->type.nominal = s->id;
        size_t bytes = strlen(a->out->name) + strlen(s->name) + 2;
        char *name = arena_alloc(&a->out->constants, bytes);
        snprintf(name, bytes, "%s.%s", a->out->name, s->name);
        script_analyzer_publish_type(
            a,
            (script_catalog_type){ .id = s->id,
                                   .name = name,
                                   .source_name = own(a, s->name),
                                   .type = s->type,
                                   .authored_record = true }
        );
    }
}
void script_analyzer_record_fields(analyzer *a, TSNode body)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (!script_analyzer_is(node, "record_declaration")) {
            continue;
        }
        int symbol = script_analyzer_lookup(a, script_analyzer_field(node, "name"));
        const script_catalog_type *found = symbol < 0
                                             ? NULL
                                             : script_catalog_type_by_id(
                                                   a->catalog,
                                                   a->out->symbols[symbol].type.nominal
                                               );
        script_range name_range = script_analyzer_range(a, script_analyzer_field(node, "name"));
        if (!found || !found->authored_record ||
            a->out->symbols[symbol].declaration.document != name_range.document ||
            a->out->symbols[symbol].declaration.start != name_range.start) {
            continue;
        }
        script_catalog_type definition = *found;
        script_catalog_field *fields = NULL;
        type_field *shape = NULL;
        TSNode members = script_analyzer_field(node, "body");
        bool valid = true;
        for (uint32_t j = 0; j < script_analyzer_named_count(members); j++) {
            TSNode member = ts_node_named_child(members, j);
            if (ts_node_is_extra(member)) {
                continue;
            }
            char *name = script_analyzer_name_copy(a, script_analyzer_field(member, "name"));
            script_catalog_field field = {
                .id = script_catalog_id("record-field", name),
                .name = own(a, name),
                .type = script_analyzer_type_node(a, script_analyzer_field(member, "type")),
                .writable = true
            };
            free(name);
            if (!*field.name || field.type.nominal || field.type.flags ||
                field.type.container != PIN_CONTAINER_SINGLE ||
                (field.type.storage != PIN_STR && field.type.storage != PIN_I32 &&
                 field.type.storage != PIN_BOOL)) {
                script_analyzer_diagnostic(a, member, "Record fields require nonoptional string, int or bool values");
                valid = false;
            }
            for (int k = 0; k < acount(fields); k++) {
                if (!strcmp(fields[k].name, field.name)) {
                    script_analyzer_diagnostic(
                        a,
                        member,
                        "Duplicate record field '%s'",
                        field.name
                    );
                    valid = false;
                }
            }
            TSNode value = script_analyzer_field(member, "value");
            field.has_default = !ts_node_is_null(value);
            if (field.has_default) {
                int expression = script_analyzer_expression(a, value, field.type);
                script_analyzer_require(a, value, field.type, script_analyzer_expr_type(a, expression));
                const char *error = script_analyzer_scalar_constant(
                    a,
                    expression,
                    field.type.storage,
                    &field.default_value
                );
                if (error) {
                    script_analyzer_diagnostic(a, value, "Record default: %s", error);
                    valid = false;
                } else if (field.default_value.type != field.type.storage) {
                    script_analyzer_diagnostic(a, value, "Record default has an incompatible scalar type");
                    valid = false;
                }
            }
            apush(fields, field);
            apush(shape, ((type_field){ .name = field.name, .type = field.type.storage }));
        }
        const pin_record *record = valid ? pin_record_create(
                                               &a->out->constants,
                                               definition.name,
                                               definition.id,
                                               shape,
                                               acount(shape)
                                           )
                                         : NULL;
        if (valid && !record) {
            script_analyzer_diagnostic(a, node, "A record requires at least one valid field");
        }
        definition.field_count = acount(fields);
        if (definition.field_count) {
            script_catalog_field *copy = arena_alloc(
                &a->out->constants,
                (size_t)definition.field_count * sizeof *copy
            );
            memcpy(copy, fields, (size_t)definition.field_count * sizeof *copy);
            if (record) {
                for (int j = 0; j < definition.field_count; j++) {
                    copy[j].offset = record->type.fields[j].offset;
                }
            }
            definition.fields = copy;
        }
        if (record) {
            definition.size = record->type.size;
            definition.layout = record->layout;
        }
        script_analyzer_replace_type(a, definition);
        afree(shape);
        afree(fields);
    }
}
