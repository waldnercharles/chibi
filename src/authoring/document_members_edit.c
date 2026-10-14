#include "authoring/document_members.h"
#include "graph/authoring/asset_edit.h"
#include "graph/document/graph_signature.h"
#include "graph/document/serialize.h"
#include "support/asset_text.h"
#include "support/text_edit.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/value_document.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *e, size_t n, const char *s)
{
    if (n) {
        snprintf(e, n, "%s", s);
    }
    return false;
}
static bool id(const char *s, uint64_t *v)
{
    if (!s || !*s) {
        return false;
    }
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    errno = 0;
    char *end;
    *v = strtoull(s, &end, 10);
    return !errno && !*end;
}
static TSNode field(TSNode n, const char *s)
{
    return ts_node_child_by_field_name(n, s, (uint32_t)strlen(s));
}
static char *span(const char *s, TSNode n)
{
    if (ts_node_is_null(n)) {
        return xstrdup("");
    }
    size_t a = ts_node_start_byte(n), b = ts_node_end_byte(n);
    char *out = xmalloc(b - a + 1);
    memcpy(out, s + a, b - a);
    out[b - a] = 0;
    return out;
}
static const source_presentation_origin *origin(const document_semantics *s, uint64_t graph)
{
    const authoring_source_presentation *p = document_semantics_presentation(s);
    for (int i = 0; p && i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->graph == graph && !o->pin && o->kind == SOURCE_PRESENTATION_FUNCTION) {
            return o;
        }
    }
    return NULL;
}
static bool source_signature(
    const document_semantics *s,
    const document_member_edit *r,
    uint64_t graph,
    char **text,
    char **metadata,
    char *e,
    size_t n
)
{
    const script_analysis *a = document_semantics_analysis(s);
    const source_presentation_origin *o = origin(s, graph);
    if (!o) {
        return fail(e, n, "Select an authored callable");
    }
    if (r->operation == DOCUMENT_MEMBER_RENAME_PIN && o->function >= 0 &&
        o->function < a->function_count) {
        const script_function *f = &a->functions[o->function];
        int count = r->result ? f->result_count : f->parameter_count;
        if (r->index < 0 || r->index >= count) {
            return fail(e, n, "Pin index does not belong to this signature");
        }
        int symbol = r->result ? f->results[r->index] : f->parameters[r->index];
        const authoring_source_presentation *p = document_semantics_presentation(s);
        if (symbol < 0 || symbol >= a->symbol_count) {
            return fail(e, n, "Parameter symbol is unavailable");
        }
        authoring_edit_request rename = { .kind = AUTHORING_EDIT_RENAME_DECLARATION,
                                          .generation = authoring_source_edit_generation(p),
                                          .origin = o->identity,
                                          .symbol = a->symbols[symbol].id,
                                          .name = r->name };
        authoring_edit_result result = { 0 };
        bool ok = document_semantics_prepare_edit(s, &rename, &result, e, n);
        if (ok) {
            *text = result.text;
            *metadata = result.metadata;
        }
        return ok;
    }

    script_syntax *syntax = script_parse(a->text);
    TSNode node = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(syntax->tree),
        o->range.start,
        o->range.end
    );
    while (!ts_node_is_null(node) &&
           strcmp(ts_node_type(node), "function_declaration") &&
           strcmp(ts_node_type(node), "macro_declaration")) {
        node = ts_node_parent(node);
    }
    if (ts_node_is_null(node)) {
        script_syntax_free(syntax);
        return fail(e, n, "Callable source origin is unavailable");
    }
    TSNode parameters = field(node, "parameters"),
           results = field(node, "results"), list = r->result ? results : parameters;
    char *parts[257] = { 0 };
    bool shorthand = !ts_node_is_null(list) &&
                     strcmp(ts_node_type(list), "parameter_list");
    int count = ts_node_is_null(list) ? 0
              : shorthand             ? 1
                                      : (int)ts_node_named_child_count(list);
    bool ok = false;
    char *replacement = NULL, *signature = NULL;
    if (count > 256 || r->operation > DOCUMENT_MEMBER_DEFAULT_PIN) {
        fail(e, n, "Unsupported callable edit or pin budget exceeded");
        goto done;
    }
    for (int i = 0; i < count; i++) {
        parts[i] = span(a->text, shorthand ? list : ts_node_named_child(list, i));
    }
    if (r->operation == DOCUMENT_MEMBER_ADD_PIN) {
        if (count == 256 || !r->name || !*r->name || !r->type || !*r->type) {
            fail(e, n, "New pin requires a name and exact type expression");
            goto done;
        }
        size_t bytes = strlen(r->name) + strlen(r->type) + 4;
        parts[count] = xmalloc(bytes);
        snprintf(parts[count++], bytes, "%s: %s", r->name, r->type);
    } else {
        if (r->index < 0 || r->index >= count) {
            fail(e, n, "Pin index does not belong to this signature");
            goto done;
        }
        if (r->operation == DOCUMENT_MEMBER_REMOVE_PIN) {
            free(parts[r->index]);
            for (int i = r->index; i < count - 1; i++) {
                parts[i] = parts[i + 1];
            }
            parts[--count] = NULL;
        } else if (r->operation == DOCUMENT_MEMBER_MOVE_PIN) {
            if (r->target < 0 || r->target >= count) {
                fail(e, n, "Destination pin index is invalid");
                goto done;
            }
            char *moving = parts[r->index];
            if (r->target > r->index) {
                for (int i = r->index; i < r->target; i++) {
                    parts[i] = parts[i + 1];
                }
            } else {
                for (int i = r->index; i > r->target; i--) {
                    parts[i] = parts[i - 1];
                }
            }
            parts[r->target] = moving;
        } else {
            TSNode pin = shorthand ? list : ts_node_named_child(list, r->index);
            TSNode chosen = field(
                pin,
                r->operation == DOCUMENT_MEMBER_RENAME_PIN   ? "name"
                : r->operation == DOCUMENT_MEMBER_RETYPE_PIN ? "type"
                                                             : "value"
            );
            const char *value = r->operation == DOCUMENT_MEMBER_RENAME_PIN ? r->name
                              : r->operation == DOCUMENT_MEMBER_RETYPE_PIN
                                  ? r->type
                                  : r->value;
            if (!value || strlen(value) > 65536 ||
                (r->result && r->operation == DOCUMENT_MEMBER_DEFAULT_PIN)) {
                fail(e, n, "Invalid signature field value");
                goto done;
            }
            int start, end;
            char *insert = NULL;
            if (shorthand) {
                if (r->operation != DOCUMENT_MEMBER_RETYPE_PIN) {
                    fail(e, n, "Name this shorthand result through the complete signature first");
                    goto done;
                }
                start = 0;
                end = (int)strlen(parts[r->index]);
            } else if (ts_node_is_null(chosen)) {
                if (r->operation != DOCUMENT_MEMBER_DEFAULT_PIN) {
                    fail(e, n, "This pin has no explicit annotation; use the complete signature");
                    goto done;
                }
                start = end = (int)strlen(parts[r->index]);
                insert = xmalloc(strlen(value) + 4);
                snprintf(insert, strlen(value) + 4, " = %s", value);
                value = insert;
            } else {
                start = (int)(ts_node_start_byte(chosen) - ts_node_start_byte(pin));
                end = (int)(ts_node_end_byte(chosen) - ts_node_start_byte(pin));
            }
            replacement = text_edits_apply(parts[r->index], &(text_edit){ start, end, value }, 1);
            free(insert);
            if (!replacement) {
                goto done;
            }
            free(parts[r->index]);
            parts[r->index] = replacement;
            replacement = NULL;
        }
    }
    size_t bytes = 3;
    for (int i = 0; i < count; i++) {
        bytes += strlen(parts[i]) + 2;
    }
    replacement = xmalloc(bytes);
    strcpy(replacement, "(");
    for (int i = 0; i < count; i++) {
        if (i) {
            strcat(replacement, ", ");
        }
        strcat(replacement, parts[i]);
    }
    strcat(replacement, ")");
    char *left = r->result ? span(a->text, parameters) : xstrdup(replacement),
         *right = r->result ? xstrdup(replacement) : span(a->text, results);
    bytes = strlen(left) + strlen(right) + 5;
    signature = xmalloc(bytes);
    snprintf(signature, bytes, "%s%s%s", left, *right ? " -> " : "", right);
    free(left);
    free(right);
    authoring_edit_request request = { .kind = AUTHORING_EDIT_SIGNATURE,
                                       .generation = authoring_source_edit_generation(
                                           document_semantics_presentation(s)
                                       ),
                                       .origin = o->identity,
                                       .signature = signature };
    authoring_edit_result result = { 0 };
    ok = document_semantics_prepare_edit(s, &request, &result, e, n);
    if (ok) {
        *text = result.text;
        *metadata = result.metadata;
    }
done:
    for (int i = 0; i < 257; i++) {
        free(parts[i]);
    }
    free(replacement);
    free(signature);
    script_syntax_free(syntax);
    return ok;
}

static bool source_member(
    const document_semantics *s,
    const document_member_edit *r,
    uint64_t graph,
    char **text,
    char **metadata,
    char *e,
    size_t n
)
{
    const authoring_source_presentation *p = document_semantics_presentation(s);
    const script_analysis *a = document_semantics_analysis(s);
    uint64_t wanted = 0;
    id(r->id, &wanted);
    const source_presentation_origin *selected = NULL;
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->identity == wanted && !o->pin) {
            selected = o;
        }
    }
    authoring_edit_request request = { .generation = authoring_source_edit_generation(p),
                                       .origin = selected ? selected->identity : 0 };
    char *fragment = NULL;
    script_syntax *syntax = NULL;
    bool ok = false;
    if (r->operation == DOCUMENT_MEMBER_RENAME_VARIABLE) {
        request.kind = AUTHORING_EDIT_RENAME_DECLARATION;
        request.name = r->name;
    } else if (r->operation == DOCUMENT_MEMBER_RETYPE_VARIABLE) {
        request.kind = AUTHORING_EDIT_RETYPE_VARIABLE;
        request.signature = r->type;
    } else if (r->operation == DOCUMENT_MEMBER_PURE) {
        selected = origin(s, graph);
        if (!selected) {
            return fail(e, n, "Select an authored function");
        }
        request.origin = selected->identity;
        request.kind = AUTHORING_EDIT_REPLACE_DECLARATION;
        syntax = script_parse(a->text);
        TSNode node = ts_node_named_descendant_for_byte_range(
            ts_tree_root_node(syntax->tree),
            selected->range.start,
            selected->range.end
        );
        while (!ts_node_is_null(node) &&
               strcmp(ts_node_type(node), "function_declaration")) {
            node = ts_node_parent(node);
        }
        if (ts_node_is_null(node)) {
            fail(e, n, "Purity applies to authored functions");
            goto done;
        }
        TSNode token = { 0 };
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            if (!strcmp(ts_node_type(child), "pure")) {
                token = child;
            }
        }
        char *original = span(a->text, node);
        int start, end;
        const char *value;
        if (ts_node_is_null(token)) {
            TSNode params = field(node, "parameters");
            start = end = (int)(ts_node_start_byte(params) - ts_node_start_byte(node));
            for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
                TSNode child = ts_node_child(node, i);
                if (!strcmp(ts_node_type(child), "fn")) {
                    start = end = (int)(ts_node_start_byte(child) -
                                        ts_node_start_byte(node));
                }
            }
            value = r->flag ? "pure " : "";
        } else {
            start = (int)(ts_node_start_byte(token) - ts_node_start_byte(node));
            end = (int)(ts_node_end_byte(token) - ts_node_start_byte(node));
            value = r->flag ? "pure" : "";
        }
        fragment = text_edits_apply(original, &(text_edit){ start, end, value }, 1);
        free(original);
        request.fragment = fragment;
    } else if (r->operation == DOCUMENT_MEMBER_REMOVE_VARIABLE || r->operation == DOCUMENT_MEMBER_DUPLICATE_VARIABLE) {
        if (!selected || selected->kind != SOURCE_PRESENTATION_SYMBOL) {
            fail(e, n, "Select an owned variable declaration");
            goto done;
        }
        if (selected->record < 0 || selected->record >= a->symbol_count) {
            fail(e, n, "Variable source origin is invalid");
            goto done;
        }
        const script_symbol *symbol = &a->symbols[selected->record];
        if (symbol->kind != SCRIPT_STATE && symbol->kind != SCRIPT_SHARED_STATE) {
            fail(e, n, "Only owned state declarations can be removed or duplicated here");
            goto done;
        }
        syntax = script_parse(a->text);
        TSNode node = ts_node_named_descendant_for_byte_range(
            ts_tree_root_node(syntax->tree),
            selected->range.start,
            selected->range.end
        );
        while (!ts_node_is_null(node) &&
               strcmp(ts_node_type(node), "state_declaration")) {
            node = ts_node_parent(node);
        }
        if (ts_node_is_null(node)) {
            fail(e, n, "Variable declaration source is unavailable");
            goto done;
        }
        if (r->operation == DOCUMENT_MEMBER_REMOVE_VARIABLE) {
            request.kind = AUTHORING_EDIT_REPLACE_DECLARATION;
            request.fragment = "";
        } else {
            TSNode name = field(node, "name");
            if (ts_node_is_null(name) || !r->name || !*r->name) {
                fail(e, n, "Duplicate requires a declaration name");
                goto done;
            }
            char *original = span(a->text, node);
            fragment = text_edits_apply(
                original,
                &(text_edit){
                    (int)(ts_node_start_byte(name) - ts_node_start_byte(node)),
                    (int)(ts_node_end_byte(name) - ts_node_start_byte(node)),
                    r->name },
                1
            );
            free(original);
            request.kind = AUTHORING_EDIT_APPEND_DECLARATIONS;
            request.fragment = fragment;
        }
    } else if (r->operation == DOCUMENT_MEMBER_DEFAULT_VARIABLE) {
        if (!selected || selected->kind != SOURCE_PRESENTATION_SYMBOL ||
            selected->record < 0 || selected->record >= a->symbol_count) {
            fail(e, n, "Select an owned variable declaration");
            goto done;
        }
        const script_symbol *symbol = &a->symbols[selected->record];
        if (symbol->initializer < 0 || symbol->initializer >= a->expression_count) {
            fail(e, n, "Variable has no editable initializer");
            goto done;
        }
        script_range range = a->expressions[symbol->initializer].range;
        const source_presentation_origin *expression = NULL;
        for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
            const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
            if (o->kind == SOURCE_PRESENTATION_EXPRESSION &&
                o->range.start == range.start && o->range.end == range.end) {
                expression = o;
            }
        }
        if (!expression) {
            fail(e, n, "Variable initializer has no semantic projection");
            goto done;
        }
        request.kind = AUTHORING_EDIT_EXPRESSION;
        request.origin = expression->identity;
        request.expression = r->value;
    } else {
        fail(e, n, "Use the source body creation capability for this operation");
        goto done;
    }
    authoring_edit_result result = { 0 };
    ok = document_semantics_prepare_edit(s, &request, &result, e, n);
    if (ok) {
        *text = result.text;
        *metadata = result.metadata;
    }
done:
    free(fragment);
    script_syntax_free(syntax);
    return ok;
}

bool document_member_prepare(
    const document_semantics *s,
    const document_member_edit *r,
    char **text,
    char **metadata,
    char *e,
    size_t n
)
{
    *text = *metadata = NULL;
    if (n) {
        *e = 0;
    }
    uint64_t graph_id = 0;
    if (!s || !r || !id(r->graph, &graph_id)) {
        return fail(e, n, "A revision-bound graph identity is required");
    }
    if (document_semantics_analysis(s)) {
        return r->operation <= DOCUMENT_MEMBER_DEFAULT_PIN
                 ? source_signature(s, r, graph_id, text, metadata, e, n)
                 : source_member(s, r, graph_id, text, metadata, e, n);
    }
    const behavior_asset *original = document_semantics_graph(s);
    behavior_asset *asset = original ? behavior_asset_clone(original) : NULL;
    graph *g = asset ? behavior_asset_find_graph(asset, graph_id) : NULL;
    bool ok = false;
    arena storage = { 0 };
    if (!g) {
        fail(e, n, "Graph does not belong to this document revision");
        goto done;
    }
    pin_spec type = { .kind = PIN_DATA,
                      .name = r->name,
                      .type = r->storage,
                      .type_name = r->type,
                      .container = r->container };
    if ((r->operation == DOCUMENT_MEMBER_ADD_PIN ||
         r->operation == DOCUMENT_MEMBER_RETYPE_PIN) &&
        (r->storage < PIN_BOOL || r->storage > PIN_ENUM || r->storage == PIN_HANDLE ||
         r->container < PIN_CONTAINER_SINGLE || r->container > PIN_CONTAINER_SET)) {
        fail(e, n, "Invalid declared pin type");
        goto done;
    }
    uint64_t member_id = 0;
    id(r->id, &member_id);
    behavior_variable *variable = behavior_asset_find_variable_by_id(asset, member_id);
    switch (r->operation) {
        case DOCUMENT_MEMBER_ADD_PIN:
            ok = !graph_signature_add(asset, g, r->result, &type);
            break;
        case DOCUMENT_MEMBER_REMOVE_PIN:
            ok = !graph_signature_remove(asset, g, r->result, r->index);
            break;
        case DOCUMENT_MEMBER_RENAME_PIN:
            ok = r->name &&
                 !graph_signature_rename(asset, g, r->result, r->index, r->name);
            break;
        case DOCUMENT_MEMBER_MOVE_PIN:
            ok = !graph_signature_move(asset, g, r->result, r->index, r->target);
            break;
        case DOCUMENT_MEMBER_RETYPE_PIN:
            ok = !graph_signature_retype(asset, g, r->result, r->index, &type);
            break;
        case DOCUMENT_MEMBER_PURE:
            ok = !graph_signature_set_pure(asset, g, r->flag);
            break;
        case DOCUMENT_MEMBER_DEFAULT_PIN: {
            node *entry = graph_node_by_id(g, g->entry_id);
            if (!r->result && entry && r->index >= 0 && r->index < entry->output_count) {
                const pin_spec *p = &entry->output_pins[r->index];
                pin_value value = { 0 }, declaration = { .type = p->type,
                                                         .type_name = p->type_name,
                                                         .container = p->container };
                av_doc *doc = r->value ? av_value_read(r->value, e, n) : NULL;
                ok = doc &&
                     !pin_value_read_document(&storage, declaration, av_doc_root(doc), &value, e, n) &&
                     !graph_signature_set_default(asset, g, r->index, &value);
                av_doc_free(doc);
            }
            break;
        }
        case DOCUMENT_MEMBER_ADD_VARIABLE: {
            if (r->storage < PIN_BOOL || r->storage > PIN_ENUM ||
                r->storage == PIN_HANDLE || r->container < PIN_CONTAINER_SINGLE ||
                r->container > PIN_CONTAINER_SET) {
                break;
            }
            pin_value declaration = { .type = r->storage,
                                      .type_name = r->type,
                                      .container = r->container },
                      value = { 0 };
            av_doc *doc = r->value ? av_value_read(r->value, e, n) : NULL;
            ok = doc &&
                 !pin_value_read_document(&storage, declaration, av_doc_root(doc), &value, e, n) &&
                 behavior_asset_add_variable(asset, r->name, r->storage, r->type, r->container, &value, "");
            av_doc_free(doc);
            break;
        }
        case DOCUMENT_MEMBER_REMOVE_VARIABLE:
            ok = variable && !behavior_asset_delete_variable(asset, member_id);
            break;
        case DOCUMENT_MEMBER_DUPLICATE_VARIABLE:
            ok = variable &&
                 behavior_asset_duplicate_variable(asset, member_id, r->name);
            break;
        case DOCUMENT_MEMBER_RENAME_VARIABLE:
            ok = variable && r->name &&
                 !behavior_asset_rename_variable(asset, member_id, r->name);
            break;
        case DOCUMENT_MEMBER_RETYPE_VARIABLE:
            ok = variable && r->storage >= PIN_BOOL && r->storage <= PIN_ENUM &&
                 r->storage != PIN_HANDLE && r->container >= PIN_CONTAINER_SINGLE &&
                 r->container <= PIN_CONTAINER_SET &&
                 !behavior_asset_retype_variable(asset, member_id, r->storage, r->type) &&
                 !behavior_asset_set_variable_container(asset, member_id, r->container);
            break;
        case DOCUMENT_MEMBER_DEFAULT_VARIABLE: {
            if (!variable) {
                break;
            }
            pin_value declaration = { .type = variable->type,
                                      .type_name = variable->type_name,
                                      .container = variable->container },
                      value = { 0 };
            av_doc *doc = r->value ? av_value_read(r->value, e, n) : NULL;
            ok = doc &&
                 !pin_value_read_document(&storage, declaration, av_doc_root(doc), &value, e, n) &&
                 !behavior_asset_set_variable_default(asset, member_id, &value);
            av_doc_free(doc);
            break;
        }
        case DOCUMENT_MEMBER_GET:
            ok = variable && behavior_asset_add_get_node_to_graph(asset, g, member_id);
            break;
        case DOCUMENT_MEMBER_SET:
            ok = variable && behavior_asset_add_set_node_to_graph(asset, g, member_id);
            break;
        case DOCUMENT_MEMBER_CALL: {
            graph *called = behavior_asset_find_graph(asset, member_id);
            ok = called &&
                 behavior_asset_add_function_call_node_to_graph(asset, g, called);
            break;
        }
        default:
            fail(e, n, "Member operation is not available for this document dialect");
            break;
    }
    if (ok) {
        *text = behavior_asset_save(asset);
        behavior_asset *check = *text ? behavior_asset_load(*text) : NULL;
        ok = check != NULL;
        behavior_asset_free(check);
    }
done:
    arena_free(&storage);
    behavior_asset_free(asset);
    if (!ok) {
        free(*text);
        *text = NULL;
        if (!n || !*e) {
            fail(e, n, "Member edit was refused");
        }
    }
    return ok;
}
