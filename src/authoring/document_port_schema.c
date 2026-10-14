#include "authoring/document_port_schema.h"

#include "authoring/document_graph.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "types/catalog.h"
#include "types/value_document.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static const char *own(arena *a, const char *s)
{
    if (!s) {
        s = "";
    }
    char *r = arena_alloc(a, strlen(s) + 1);
    strcpy(r, s);
    return r;
}
static const pin_spec *find(
    const document_semantics *s,
    uint64_t graph_id,
    uint64_t node_id,
    uint64_t pin_id,
    bool output,
    const node **owner
)
{
    const behavior_asset *asset = document_semantics_graph(s);
    for (int g = 0; asset && g < acount(asset->graphs); g++) {
        const graph *graph = asset->graphs[g];
        if (graph->graph_id != graph_id) {
            continue;
        }
        for (int n = 0; n < acount(graph->nodes); n++) {
            const node *node = graph->nodes[n];
            if (node->id != node_id) {
                continue;
            }
            int count = output ? node->output_count : node->input_count;
            const pin_spec *pins = output ? node->output_pins : node->input_pins;
            for (int p = 0; p < count; p++) {
                if (pins[p].id == pin_id) {
                    *owner = node;
                    return &pins[p];
                }
            }
        }
    }
    return NULL;
}
typedef struct
{
    const script_catalog *catalog;
    arena *storage;
    document_port_field *fields;
    document_port_choice *choices;
    int first, count, field_count, choice_count, total, choice_capacity;
    bool editable;
} builder;
static bool append(
    builder *b,
    const char *path,
    const char *name,
    pin_type storage,
    pin_container container,
    const char *type,
    uint64_t nominal,
    const type_field *native,
    const script_catalog_field *source,
    const av_value *value,
    bool color,
    int depth
)
{
    if (depth > 16 || b->total >= 4096) {
        return false;
    }
    const type_desc *layout = storage == PIN_STRUCT && type ? type_catalog_find(type)
                                                            : NULL;
    const script_catalog_type *record = script_catalog_type_by_id(b->catalog, nominal);
    if (storage == PIN_STRUCT && container == PIN_CONTAINER_SINGLE &&
        ((record && record->field_count) || layout)) {
        int count = record ? record->field_count : layout->field_count;
        for (int i = 0; i < count; i++) {
            const script_catalog_field *sf = record ? &record->fields[i] : NULL;
            const type_field *nf = record ? NULL : &layout->fields[i];
            const char *key = sf ? sf->name : nf->name;
            char child[1024];
            int written = snprintf(child, sizeof child, "%s%s%s", path, *path ? "." : "", key);
            if (written < 0 || (size_t)written >= sizeof child) {
                return false;
            }
            const script_catalog_type
                *nested = sf ? script_catalog_type_by_id(b->catalog, sf->type.nominal)
                             : NULL;
            if (!append(
                    b,
                    child,
                    key,
                    sf ? sf->type.storage : nf->type,
                    sf ? sf->type.container : PIN_CONTAINER_SINGLE,
                    sf ? (nested ? nested->name : pin_type_name(sf->type.storage))
                       : nf->type_name,
                    sf ? sf->type.nominal : 0,
                    nf,
                    sf,
                    av_obj_get(value, key),
                    color || (nf && nf->color),
                    depth + 1
                )) {
                return false;
            }
        }
        return true;
    }
    int index = b->total++;
    if (index < b->first || index - b->first >= b->count) {
        return true;
    }
    char *literal = value ? av_value_write(value) : NULL;
    b->fields[b->field_count++] = (document_port_field){
        .path = own(b->storage, path),
        .label = own(b->storage, name),
        .type = own(b->storage, type ? type : pin_type_name(storage)),
        .unit = own(
            b->storage,
            source   ? source->unit
            : native ? native->unit
                     : NULL
        ),
        .value = own(b->storage, literal),
        .storage = storage,
        .container = container,
        .min = source ? source->min
             : native ? native->min
                      : 0,
        .max = source ? source->max
             : native ? native->max
                      : 0,
        .range = source ? source->min != source->max
                        : native && native->min != native->max,
        .range_contract = source ? source->range_contract : native && native->range_contract,
        .color = color || (native && native->color),
        .editable = b->editable && container == PIN_CONTAINER_SINGLE
    };
    free(literal);
    const enum_desc *enumeration = type && (storage == PIN_ENUM || storage == PIN_I32)
                                     ? type_catalog_enum_find(type)
                                     : NULL;
    for (int i = 0; enumeration && i < enumeration->value_count; i++) {
        if (b->choice_count >= b->choice_capacity) {
            return false;
        }
        const enum_value_desc *v = &enumeration->values[i];
        char numeric[32];
        snprintf(numeric, sizeof numeric, "%d", v->value);
        b->choices[b->choice_count++] = (document_port_choice){
            own(b->storage, path),
            own(b->storage, v->label ? v->label : v->name),
            own(b->storage, v->name),
            own(b->storage, numeric)
        };
    }
    return true;
}
bool document_port_schema(
    const document_semantics *s,
    uint64_t graph_id,
    uint64_t node_id,
    uint64_t pin_id,
    bool output,
    int first,
    int count,
    arena *a,
    document_port_field **fields,
    int *field_count,
    document_port_choice **choices,
    int *choice_count,
    int *total,
    char *error,
    size_t size
)
{
    *fields = NULL;
    *choices = NULL;
    *field_count = *choice_count = *total = 0;
    if (size) {
        *error = 0;
    }
    if (first < 0 || count < 0 || count > 256) {
        return fail(error, size, "Port schema requires First>=0 and Count in0..256");
    }
    const node *n = NULL;
    const pin_spec *pin = find(s, graph_id, node_id, pin_id, output, &n);
    if (!pin || pin->kind != PIN_DATA) {
        return fail(error, size, "Select a current data pin");
    }
    const authoring_source_presentation *presentation = document_semantics_presentation(s);
    const source_presentation_origin *
        origin = presentation
                   ? authoring_source_presentation_origin(presentation, graph_id, node_id, pin_id)
                   : NULL;
    builder b = { .catalog = document_semantics_catalog(s),
                  .storage = a,
                  .first = first,
                  .count = count,
                  .fields = arena_alloc(a, (size_t)count * sizeof *b.fields),
                  .choice_capacity = 4096,
                  .choices = arena_alloc(a, 4096 * sizeof *b.choices),
                  .editable = !presentation &&
                              (!output || n->desc->pure_kind == NODE_PURE_CONST) };
    const type_field *field = NULL;
    if (n->desc->type && ((!output && n->desc->pure_kind == NODE_PURE_MAKE) ||
                          (output && n->desc->pure_kind == NODE_PURE_BREAK))) {
        for (int i = 0; i < n->desc->type->field_count; i++) {
            if (!strcmp(n->desc->type->fields[i].name, pin->name)) {
                field = &n->desc->type->fields[i];
                break;
            }
        }
    }
    const script_catalog_field *source_field = NULL;
    const script_analysis *analysis = document_semantics_analysis(s);
    if (origin && analysis && origin->kind == SOURCE_PRESENTATION_EXPRESSION &&
        origin->record >= 0 && origin->record < analysis->expression_count) {
        uint64_t member = analysis->expressions[origin->record].member;
        for (int i = 0; member && i < b.catalog->type_count; i++) {
            const script_catalog_type *type = &b.catalog->types[i];
            for (int j = 0; j < type->field_count; j++) {
                if (type->fields[j].id == member) {
                    source_field = &type->fields[j];
                }
            }
        }
    }
    av_doc *document = av_doc_new();
    const av_value *value = presentation
                              ? NULL
                              : pin_value_to_document(document, &pin->default_value);
    bool ok = append(
        &b,
        "",
        pin->name,
        pin->type,
        pin->container,
        pin->type_name,
        origin ? origin->type.nominal : 0,
        field,
        source_field,
        value,
        (pin->flags & PIN_FLAG_COLOR) != 0,
        0
    );
    av_doc_free(document);
    if (!ok) {
        return fail(error, size, "Port schema exceeds reflected nesting, field or enum choice limits");
    }
    *fields = b.fields;
    *field_count = b.field_count;
    *choices = b.choices;
    *choice_count = b.choice_count;
    *total = b.total;
    return true;
}
bool document_port_prepare_field(
    workspace_documents *docs,
    int document,
    const document_semantics *s,
    uint64_t graph_id,
    uint64_t node_id,
    uint64_t pin_id,
    bool output,
    const char *path,
    const char *literal,
    char **text,
    char *error,
    size_t size
)
{
    *text = NULL;
    if (size) {
        *error = 0;
    }
    if (document_semantics_presentation(s)) {
        return fail(error, size, "Source inputs require an exact source expression edit");
    }
    const node *n = NULL;
    const pin_spec *pin = find(s, graph_id, node_id, pin_id, output, &n);
    if (!pin || pin->kind != PIN_DATA ||
        (output && n->desc->pure_kind != NODE_PURE_CONST)) {
        return fail(error, size, "Select an editable current default pin");
    }
    if (!path || strlen(path) >= 1024) {
        return fail(error, size, "Invalid reflected field path");
    }
    av_doc *value = av_value_read(literal, error, size);
    if (!value) {
        return false;
    }
    av_doc *aggregate = av_doc_new();
    av_value *root = pin_value_to_document(aggregate, &pin->default_value),
             *target = root;
    bool ok = true;
    char keys[1024];
    strcpy(keys, path);
    char *key = keys;
    while (*key) {
        char *dot = strchr(key, '.');
        if (dot) {
            *dot = 0;
        }
        if (!*key || !av_obj_get(target, key)) {
            ok = false;
            break;
        }
        if (!dot) {
            ok = av_obj_set(aggregate, target, key, av_clone(aggregate, av_doc_root(value)));
            break;
        }
        target = (av_value *)av_obj_get(target, key);
        key = dot + 1;
        if (!*key) {
            ok = false;
            break;
        }
    }
    char *candidate = ok ? av_value_write(*path ? root : av_doc_root(value)) : NULL;
    if (ok && !candidate) {
        ok = false;
    }
    if (ok) {
        document_graph_request request = { .operation = DOCUMENT_GRAPH_DEFAULT,
                                           .graph = graph_id,
                                           .node = node_id,
                                           .pin = pin_id,
                                           .value = candidate };
        uint64_t created;
        ok = document_graph_prepare(
            docs,
            document,
            document_semantics_revision(s),
            &request,
            &created,
            text,
            error,
            size
        );
    }
    free(candidate);
    av_doc_free(aggregate);
    av_doc_free(value);
    if (!ok && size && !*error) {
        fail(error, size, "Invalid reflected field value or path");
    }
    return ok;
}
