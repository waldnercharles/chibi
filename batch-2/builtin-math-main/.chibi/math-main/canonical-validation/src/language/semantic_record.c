#include "language/nominal_binding.h"
#include "language/semantic_internal.h"
#include "support/array.h"
#include "types/record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool script_analyzer_record_field(analyzer *a, TSNode node, script_type type, const char *name, type_field *shape)
{
    const script_catalog_type *nominal = script_catalog_type_by_id(a->catalog, type.nominal);
    *shape = (type_field){ .name = name,
                           .type = type.storage,
                           .type_name = nominal ? nominal->name : NULL };
    bool pointer = type.storage == PIN_POINTER &&
                   type.container == PIN_CONTAINER_SINGLE && !type.collection;
    if (type.container != PIN_CONTAINER_SINGLE || type.collection ||
        (type.flags & ~(SCRIPT_TYPE_LITERAL | SCRIPT_TYPE_UNTYPED |
                        (pointer ? SCRIPT_TYPE_OPTIONAL : 0)))) {
        script_analyzer_diagnostic(a, node, "Record fields require concrete nonoptional value layouts; collections and references are not supported");
        return false;
    }
    return true; /* pin_record_create validates scalar/native nested physical layouts. */
}

static const char *own(analyzer *a, const char *text)
{
    size_t bytes = strlen(text) + 1;
    char *copy = arena_alloc(&a->out->constants, bytes);
    memcpy(copy, text, bytes);
    return copy;
}
void script_analyzer_record_types(analyzer *a, TSNode body)
{
    script_analyzer_generic_prepare(a, body);
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (!script_analyzer_is(node, "record_declaration")) {
            continue;
        }
        if (!ts_node_is_null(script_analyzer_field(node, "parameters"))) {
            continue;
        }
        TSNode token = script_analyzer_field(node, "name");
        script_nominal_context context = { .lexical = { a->scope,
                                                        a->function,
                                                        a->package_scope,
                                                        a->document_count,
                                                        a->specialization },
                                           .program_kind = a->out->kind,
                                           .program_name = a->out->name,
                                           .pointer_size = sizeof(void *) };
        char *name = script_nominal_allowed(context, SCRIPT_NOMINAL_RECORD)
                       ? script_analyzer_name_copy(a, token)
                       : NULL;
        script_lexical_facts facts = script_lexical_view(a->out, acount(a->out->symbols));
        script_range range = script_analyzer_range(a, token);
        script_nominal_declaration declaration = script_nominal_declare(
            &a->out->constants,
            &facts,
            context,
            (script_nominal_request){ .kind = SCRIPT_NOMINAL_RECORD,
                                      .name = name,
                                      .range = script_analyzer_range(a, node),
                                      .name_range = range }
        );
        for (int j = 0; j < declaration.diagnostic_count; j++) {
            apush(a->out->diagnostics, declaration.diagnostics[j]);
        }
        int symbol = declaration.existing;
        if (declaration.declare && symbol < 0) {
            apush(
                a->out->symbols,
                ((script_symbol){ .id = declaration.id,
                                  .name = name,
                                  .kind = SCRIPT_TYPE,
                                  .type = declaration.declaration_type,
                                  .declaration = range,
                                  .scope = a->scope,
                                  .initializer = -1,
                                  .function = -1,
                                  .writable = false })
            );
            symbol = acount(a->out->symbols) - 1;
            name = NULL; /* The analysis now owns this heap symbol name. */
        }
        free(name);
        if (!declaration.publish) {
            continue;
        }
        a->out->symbols[symbol].type = declaration.type;
        script_analyzer_publish_type(
            a,
            (script_catalog_type){ .id = declaration.id,
                                   .name = declaration.name,
                                   .source_name = declaration.source_name,
                                   .type = declaration.type,
                                   .authored_record = declaration.authored_record,
                                   .size = declaration.size }
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
        if (!ts_node_is_null(script_analyzer_field(node, "parameters"))) {
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
            type_field layout;
            valid &= script_analyzer_record_field(a, member, field.type, field.name, &layout);
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
            if (field.type.storage == PIN_POINTER &&
                field.type.container == PIN_CONTAINER_SINGLE &&
                (field.type.flags & SCRIPT_TYPE_OPTIONAL) &&
                (ts_node_is_null(value) || script_analyzer_is(value, "nil"))) {
                field.has_default = true;
                const script_catalog_type
                    *pointer = script_catalog_type_by_id(a->catalog, field.type.nominal);
                field.default_value = (pin_value){ .type = PIN_POINTER,
                                                   .type_name = pointer
                                                                  ? pointer->name
                                                                  : NULL };
            } else if (field.has_default) {
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
            apush(shape, layout);
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
