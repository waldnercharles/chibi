#include "authoring/source_navigation.h"

#include "authoring/source_prototype_flow.h"
#include "runtime/input_action.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/text.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct authoring_navigation
{
    authoring_document *documents;
    const script_catalog *catalog;
    authoring_reference *references;
    int *reference_heads, *reference_previous;
    authoring_call *calls;
    const char **texts;
    source_text_kind *text_kinds;
};

const script_catalog_template *authoring_navigation_template(const authoring_navigation *index, uint64_t id)
{
    for (int i = 0; i < index->catalog->specialization_count; i++) {
        if (index->catalog->specializations[i].id == id) {
            id = index->catalog->specializations[i].template_id;
            break;
        }
    }
    for (int i = 0; i < index->catalog->template_count; i++) {
        if (index->catalog->templates[i].id == id) {
            return &index->catalog->templates[i];
        }
    }
    return NULL;
}

bool authoring_target_equal(authoring_target a, authoring_target b)
{
    return a.kind == b.kind && a.document == b.document && a.id == b.id &&
           a.owner == b.owner;
}

static bool callable(const script_symbol *symbol)
{
    return symbol->kind == SCRIPT_FUNCTION || symbol->kind == SCRIPT_MACRO;
}

static authoring_target symbol_target(int document, const script_symbol *symbol)
{
    if (callable(symbol) ||
        (symbol->declaration.start < 0 &&
         (symbol->kind == SCRIPT_EVENT || symbol->kind == SCRIPT_ACTION))) {
        return (authoring_target){ AUTHORING_CALLABLE,
                                   -1,
                                   symbol->capability ? symbol->capability
                                                      : symbol->id,
                                   0 };
    }
    return (authoring_target){ AUTHORING_SYMBOL, document, symbol->id, 0 };
}

static authoring_target program_target(const script_analysis *analysis)
{
    return (
        authoring_target
    ){ AUTHORING_PROGRAM,
       -1,
       analysis->identity ? script_catalog_id("program", analysis->identity) : 0,
       (uint64_t)analysis->kind };
}

static const script_function *called_function(const authoring_navigation *index, uint64_t id, int *document)
{
    for (int d = 0; d < acount(index->documents); d++) {
        const script_analysis *analysis = index->documents[d].analysis;
        if (!analysis) {
            continue;
        }
        for (int i = 0; i < analysis->function_count; i++) {
            const script_function *function = &analysis->functions[i];
            if (function->symbol >= 0 &&
                callable(&analysis->symbols[function->symbol]) &&
                analysis->symbols[function->symbol].id == id) {
                *document = d;
                return function;
            }
        }
    }
    return NULL;
}

static const script_symbol *find_symbol(const authoring_navigation *index, authoring_target target)
{
    for (int d = 0; d < acount(index->documents); d++) {
        if (target.document >= 0 && d != target.document) {
            continue;
        }
        const script_analysis *analysis = index->documents[d].analysis;
        if (!analysis) {
            continue;
        }
        for (int i = 0; i < analysis->symbol_count; i++) {
            if (authoring_target_equal(symbol_target(d, &analysis->symbols[i]), target)) {
                return &analysis->symbols[i];
            }
        }
    }
    return NULL;
}

static authoring_target member_target(const authoring_navigation *index, int document, uint64_t id, uint64_t owner)
{
    const script_analysis *analysis = index->documents[document].analysis;
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].id == id) {
            return symbol_target(document, &analysis->symbols[i]);
        }
    }
    return (authoring_target){ AUTHORING_FIELD, -1, id, owner };
}

static void add_reference(authoring_navigation *index, authoring_reference reference)
{
    const script_catalog_template
        *definition = authoring_navigation_template(index, reference.target.id);
    if (definition) {
        if (reference.kind == AUTHORING_DEFINE &&
            reference.target.id != definition->id) {
            return; /* Generated declarations reuse ranges from another source. */
        }
        reference.target = (authoring_target){ definition->kind == SCRIPT_TEMPLATE_RECORD
                                                   ? AUTHORING_TYPE
                                                   : AUTHORING_CALLABLE,
                                               -1,
                                               definition->id,
                                               0 };
    }
    if (!reference.target.id || reference.location.range.start < 0 ||
        reference.location.range.end <= reference.location.range.start) {
        return;
    }
    while (acount(index->reference_heads) <= reference.location.document) {
        apush(index->reference_heads, -1);
    }
    /* Catalog/template definitions arrive after the initial document walk,
     * so references to one document are not necessarily contiguous. */
    for (int i = index->reference_heads[reference.location.document]; i >= 0;
         i = index->reference_previous[i]) {
        const authoring_reference *previous = &index->references[i];
        if (previous->kind == reference.kind &&
            previous->location.range.start == reference.location.range.start &&
            previous->location.range.end == reference.location.range.end &&
            authoring_target_equal(previous->target, reference.target)) {
            return;
        }
    }
    apush(index->reference_previous, index->reference_heads[reference.location.document]);
    index->reference_heads[reference.location.document] = acount(index->references);
    apush(index->references, reference);
}

static TSNode expression_node(const script_syntax *syntax, script_range range)
{
    TSNode root = ts_tree_root_node(syntax->tree);
    if (range.start < 0 || range.end <= range.start ||
        (uint32_t)range.end > ts_node_end_byte(root)) {
        return (TSNode){ 0 };
    }
    TSNode node = ts_node_named_descendant_for_byte_range(
        root,
        (uint32_t)range.start,
        (uint32_t)range.end
    );
    if (ts_node_start_byte(node) != (uint32_t)range.start ||
        ts_node_end_byte(node) != (uint32_t)range.end) {
        return (TSNode){ 0 };
    }
    return node;
}

static TSNode name_node(TSNode node)
{
    if (ts_node_is_null(node)) {
        return node;
    }
    if (!strcmp(ts_node_type(node), "member_expression")) {
        return ts_node_child_by_field_name(node, "member", 6);
    }
    if (!strcmp(ts_node_type(node), "engine_reference")) {
        return ts_node_child_by_field_name(node, "name", 4);
    }
    return node;
}

static int function_at(const script_analysis *analysis, script_range range)
{
    int found = -1;
    for (int i = 0; i < analysis->function_count; i++) {
        script_range candidate = analysis->functions[i].range;
        if (candidate.start <= range.start && candidate.end >= range.end &&
            (found < 0 || candidate.end - candidate.start <
                              analysis->functions[found].range.end -
                                  analysis->functions[found].range.start)) {
            found = i;
        }
    }
    return found;
}

static void call_names(authoring_navigation *index, int document, const script_expression *expression, TSNode node)
{
    int owner = -1;
    const script_function *function = called_function(index, expression->capability, &owner);
    if (!function) {
        return;
    }
    const script_analysis *callee = index->documents[owner].analysis;
    TSNode target = ts_node_child_by_field_name(node, "function", 8);
    if (!expression->instance && callee->kind == SCRIPT_PROGRAM_LIBRARY &&
        !ts_node_is_null(target) &&
        !strcmp(ts_node_type(target), "member_expression")) {
        TSNode object = ts_node_child_by_field_name(target, "object", 6);
        add_reference(
            index,
            (authoring_reference){ program_target(callee),
                                   { document, script_node_range(object) },
                                   AUTHORING_READ,
                                   { .storage = PIN_FLOW } }
        );
    }
    const authoring_document *source = &index->documents[document];
    for (int i = 0; i < expression->argument_count; i++) {
        const script_argument *argument = &expression->arguments[i];
        if (argument->parameter < 0 || argument->parameter >= function->parameter_count) {
            continue;
        }
        TSNode actual = expression_node(source->syntax, argument->range);
        if (ts_node_is_null(actual)) {
            continue;
        }
        TSNode name = ts_node_child_by_field_name(actual, "name", 4);
        if (ts_node_is_null(name)) {
            continue;
        }
        if (!strcmp(ts_node_type(name), "argument_name") &&
            ts_node_named_child_count(name)) {
            name = ts_node_named_child(name, 0);
        }
        const script_symbol
            *parameter = &callee->symbols[function->parameters[argument->parameter]];
        add_reference(
            index,
            (authoring_reference){ symbol_target(owner, parameter),
                                   { document, script_node_range(name) },
                                   AUTHORING_READ,
                                   parameter->type }
        );
    }
}

static bool member_written(const script_analysis *analysis, const script_expression *expression)
{
    for (int i = 0; i < analysis->use_count; i++) {
        const script_use *use = &analysis->uses[i];
        if (use->kind == SCRIPT_WRITE && use->member == expression->member &&
            use->range.start == expression->range.start &&
            use->range.end == expression->range.end) {
            return true;
        }
    }
    return false;
}

/* `input.<Action>` folds to the action's name, and an action is the event
 * of that name, which the input map defines. */
static authoring_target action_target(const authoring_navigation *index, const char *name)
{
    int event = script_catalog_find_capability(index->catalog, name, SCRIPT_CAPABILITY_EVENT);
    return (authoring_target){ AUTHORING_CALLABLE,
                               -1,
                               event >= 0 ? index->catalog->capabilities[event].id : 0,
                               0 };
}

static void expressions(authoring_navigation *index, int document)
{
    const authoring_document *source = &index->documents[document];
    const script_analysis *analysis = source->analysis;
    const uint64_t action_type = script_catalog_id("type", INPUT_ACTION_TYPE);
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *expression = &analysis->expressions[i];
        TSNode node = expression_node(source->syntax, expression->range);
        if (ts_node_is_null(node)) {
            continue;
        }
        authoring_target target;
        authoring_reference_kind kind = AUTHORING_READ;
        if (expression->kind == SCRIPT_EXPR_CALL && expression->capability) {
            if (strcmp(ts_node_type(node), "call_expression")) {
                continue;
            }
            call_names(index, document, expression, node);
            node = name_node(ts_node_child_by_field_name(node, "function", 8));
            target = (authoring_target){ AUTHORING_CALLABLE, -1, expression->capability, 0 };
            const script_catalog_template
                *definition = authoring_navigation_template(index, target.id);
            if (definition) {
                target.id = definition->id;
            }
            kind = AUTHORING_CALL;
            apush(
                index->calls,
                ((authoring_call){ { document, expression->range },
                                   function_at(analysis, expression->range),
                                   target })
            );
            if (expression->instance) {
                continue; /* The source token names its configured action alias. */
            }
        } else if (
            expression->kind == SCRIPT_EXPR_MEMBER && expression->member &&
            expression->left >= 0
        ) {
            if (strcmp(ts_node_type(node), "member_expression")) {
                continue;
            }
            node = name_node(node);
            uint64_t owner = analysis->expressions[expression->left].type.nominal;
            target = member_target(index, document, expression->member, owner);
            int result_document = -1;
            const script_function *producer = called_function(index, owner, &result_document);
            if (producer && expression->offset < (size_t)producer->result_count &&
                (analysis->expressions[expression->left].type.flags &
                 SCRIPT_TYPE_RESULTS)) {
                const script_analysis
                    *result_analysis = index->documents[result_document].analysis;
                target = symbol_target(
                    result_document,
                    &result_analysis->symbols[producer->results[expression->offset]]
                );
            }
            if (member_written(analysis, expression)) {
                kind = AUTHORING_WRITE;
            }
        } else if (expression->kind == SCRIPT_EXPR_COMPONENT && expression->capability && !strcmp(ts_node_type(node), "member_expression")) {
            node = name_node(node);
            target = (authoring_target){ AUTHORING_TYPE, -1, expression->capability, 0 };
        } else if (
            expression->kind == SCRIPT_EXPR_CONSTANT && expression->member &&
            expression->type.nominal == action_type &&
            !strcmp(ts_node_type(node), "member_expression")
        ) {
            node = name_node(node);
            arena storage = { 0 };
            const char *name;
            const char *error = pin_text_cstring(&storage, expression->constant, &name, NULL);
            if (!error) {
                target = action_target(index, name);
            }
            arena_free(&storage);
            if (error) {
                continue;
            }
        } else {
            continue;
        }
        if (!ts_node_is_null(node)) {
            add_reference(
                index,
                (authoring_reference){ target,
                                       { document, script_node_range(node) },
                                       kind,
                                       expression->type }
            );
        }
    }
}

static void document_references(authoring_navigation *index, int document)
{
    const script_analysis *analysis = index->documents[document].analysis;
    TSNode root = ts_tree_root_node(index->documents[document].syntax->tree);
    for (uint32_t i = 0; i < ts_node_named_child_count(root); i++) {
        TSNode program = ts_node_named_child(root, i);
        const char *kind = ts_node_type(program);
        if (strcmp(kind, "behavior") && strcmp(kind, "library") &&
            strcmp(kind, "system")) {
            continue;
        }
        TSNode name = ts_node_child_by_field_name(program, "name", 4);
        if (!ts_node_is_null(name)) {
            add_reference(
                index,
                (authoring_reference){ program_target(analysis),
                                       { document, script_node_range(name) },
                                       AUTHORING_DEFINE,
                                       { .storage = PIN_FLOW } }
            );
        }
        break;
    }
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        add_reference(
            index,
            (authoring_reference){ symbol_target(document, symbol),
                                   { document, symbol->declaration },
                                   AUTHORING_DEFINE,
                                   symbol->type }
        );
    }
    for (int i = 0; i < analysis->use_count; i++) {
        const script_use *use = &analysis->uses[i];
        const script_symbol *symbol = use->symbol >= 0
                                        ? &analysis->symbols[use->symbol]
                                        : NULL;
        authoring_target target;
        if (symbol && (symbol->kind == SCRIPT_ACTION || symbol->kind == SCRIPT_EVENT)) {
            target = symbol_target(document, symbol);
        } else if (use->kind == SCRIPT_TYPE_USE) {
            target = symbol
                       ? symbol_target(document, symbol)
                       : (authoring_target){ AUTHORING_TYPE, -1, use->member, 0 };
        } else if (use->member) {
            if (use->kind != SCRIPT_CALL && use->kind != SCRIPT_HANDLE) {
                continue; /* Member expression supplies its nominal owner and exact name. */
            }
            const script_catalog_capability
                *capability = script_catalog_capability_by_id(index->catalog, use->member);
            target = (authoring_target){ capability && capability->kind == SCRIPT_CAPABILITY_PROTOTYPE
                                             ? AUTHORING_PROTOTYPE
                                             : AUTHORING_CALLABLE,
                                         -1,
                                         use->member,
                                         0 };
        } else if (symbol) {
            target = symbol_target(document, symbol);
        } else {
            continue;
        }
        authoring_reference_kind kind = use->kind == SCRIPT_WRITE ? AUTHORING_WRITE
                                      : use->kind == SCRIPT_CALL ? AUTHORING_CALL
                                      : use->kind == SCRIPT_HANDLE
                                          ? AUTHORING_HANDLE
                                          : AUTHORING_READ;
        script_type type = symbol ? symbol->type
                                  : (script_type){ .flags = SCRIPT_TYPE_ERROR };
        script_range range = use->range;
        if (kind == AUTHORING_CALL) {
            TSNode node = expression_node(index->documents[document].syntax, range);
            if (symbol && symbol->kind == SCRIPT_ACTION && !ts_node_is_null(node) &&
                !strcmp(ts_node_type(node), "member_expression")) {
                node = ts_node_child_by_field_name(node, "object", 6);
            } else {
                node = name_node(node);
            }
            if (!ts_node_is_null(node)) {
                range = script_node_range(node);
            }
        }
        add_reference(index, (authoring_reference){ target, { document, range }, kind, type });
    }
    expressions(index, document);
}

/* Constants that reach a prototype parameter through values reference it. */
static void prototype_names(authoring_navigation *index)
{
    authoring_prototype_name *names = authoring_prototype_names(
        index->documents,
        acount(index->documents),
        index->catalog
    );
    for (int i = 0; i < acount(names); i++) {
        add_reference(
            index,
            (authoring_reference){ { AUTHORING_PROTOTYPE, -1, names[i].prototype, 0 },
                                   { names[i].document, names[i].range },
                                   AUTHORING_HANDLE,
                                   { .storage = PIN_NAME } }
        );
    }
    afree(names);
}

authoring_navigation *authoring_navigation_new(
    const authoring_document *documents,
    int count,
    const script_catalog *catalog
)
{
    return authoring_navigation_new_declarations(documents, count, catalog, NULL, 0);
}

authoring_navigation *authoring_navigation_new_declarations(
    const authoring_document *documents,
    int count,
    const script_catalog *catalog,
    const source_catalog_definition *definitions,
    int definition_count
)
{
    return authoring_navigation_new_project(documents, count, catalog, definitions, definition_count, NULL, 0);
}

static int external_document(authoring_navigation *index, const char *path, const char *text, source_text_kind kind)
{
    for (int i = 0; i < acount(index->documents); i++) {
        if (!strcmp(index->documents[i].path, path)) {
            assert(!strcmp(index->texts[i], text));
            assert(index->text_kinds[i] == kind);
            return i;
        }
    }
    int document = acount(index->documents);
    apush(index->documents, ((authoring_document){ .path = path }));
    apush(index->texts, text);
    apush(index->text_kinds, kind);
    return document;
}

authoring_navigation *authoring_navigation_new_project(
    const authoring_document *documents,
    int count,
    const script_catalog *catalog,
    const source_catalog_definition *definitions,
    int definition_count,
    const source_catalog_reference *references,
    int reference_count
)
{
    assert(count >= 0 && (documents || !count) && catalog);
    assert(definition_count >= 0 && (definitions || !definition_count));
    authoring_navigation *index = xcalloc(1, sizeof *index);
    index->catalog = catalog;
    assert(reference_count >= 0 && (references || !reference_count));
    for (int i = 0; i < count; i++) {
        assert(documents[i].path && documents[i].syntax && documents[i].analysis);
        assert(!strcmp(documents[i].syntax->text, documents[i].analysis->text));
        apush(index->documents, documents[i]);
        apush(index->texts, documents[i].syntax->text);
        apush(index->text_kinds, SOURCE_TEXT_CHIBI);
    }
    for (int i = 0; i < count; i++) {
        document_references(index, i);
    }
    for (int i = 0; i < catalog->template_count; i++) {
        const script_catalog_template *definition = &catalog->templates[i];
        for (int d = 0; d < count; d++) {
            const script_analysis *analysis = documents[d].analysis;
            if (!definition->owner || !analysis->identity ||
                strcmp(definition->owner, analysis->identity)) {
                continue;
            }
            TSNode declaration = expression_node(
                documents[d].syntax,
                (script_range){ .start = definition->start, .end = definition->end }
            );
            TSNode name = ts_node_child_by_field_name(declaration, "name", 4);
            if (!ts_node_is_null(name)) {
                add_reference(
                    index,
                    (authoring_reference){ { definition->kind == SCRIPT_TEMPLATE_RECORD
                                                 ? AUTHORING_TYPE
                                                 : AUTHORING_CALLABLE,
                                             -1,
                                             definition->id,
                                             0 },
                                           { d, script_node_range(name) },
                                           AUTHORING_DEFINE,
                                           { 0 } }
                );
            }
        }
    }
    prototype_names(index);
    for (int i = 0; i < definition_count; i++) {
        const source_catalog_definition *definition = &definitions[i];
        assert(definition->path && definition->text);
        int document = external_document(
            index,
            definition->path,
            definition->text,
            definition->text_kind
        );
        authoring_target target = { definition->relationship ? AUTHORING_TYPE
                                    : definition->field      ? AUTHORING_FIELD
                                    : definition->kind == SCRIPT_CAPABILITY_PROTOTYPE
                                        ? AUTHORING_PROTOTYPE
                                        : AUTHORING_CALLABLE,
                                    -1,
                                    definition->id,
                                    definition->owner };
        script_type type = { .storage = PIN_FLOW };
        if (definition->field) {
            const script_catalog_type
                *owner = script_catalog_type_by_id(catalog, definition->owner);
            assert(owner);
            int found = -1;
            for (int field = 0; field < owner->field_count; field++) {
                if (owner->fields[field].id == definition->id) {
                    found = field;
                    break;
                }
            }
            assert(found >= 0);
            type = owner->fields[found].type;
        }
        add_reference(
            index,
            (authoring_reference){ target, { document, definition->range }, AUTHORING_DEFINE, type }
        );
    }
    for (int i = 0; i < reference_count; i++) {
        const source_catalog_reference *reference = &references[i];
        assert(reference->path && reference->text);
        int document = external_document(
            index,
            reference->path,
            reference->text,
            reference->text_kind
        );
        authoring_target_kind kind = reference->kind == SOURCE_REFERENCE_PROGRAM ||
                                             reference->kind == SOURCE_REFERENCE_SYSTEM
                                       ? AUTHORING_PROGRAM
                                   : reference->kind == SOURCE_REFERENCE_RELATIONSHIP
                                       ? AUTHORING_TYPE
                                   : reference->kind == SOURCE_REFERENCE_PROTOTYPE
                                       ? AUTHORING_PROTOTYPE
                                       : AUTHORING_CALLABLE;
        add_reference(
            index,
            (authoring_reference){ { kind, -1, reference->id, reference->owner },
                                   { document, reference->range },
                                   AUTHORING_READ,
                                   { .storage = PIN_FLOW } }
        );
    }
    return index;
}

void authoring_navigation_free(authoring_navigation *index)
{
    if (!index) {
        return;
    }
    afree(index->documents);
    afree(index->texts);
    afree(index->text_kinds);
    afree(index->references);
    afree(index->reference_heads);
    afree(index->reference_previous);
    afree(index->calls);
    free(index);
}

int authoring_navigation_document_count(const authoring_navigation *index)
{
    return acount(index->documents);
}

const authoring_document *authoring_navigation_document_at(const authoring_navigation *index, int i)
{
    assert(i >= 0 && i < acount(index->documents));
    return &index->documents[i];
}

const script_catalog *authoring_navigation_catalog(const authoring_navigation *index)
{
    return index->catalog;
}

const char *authoring_navigation_document_text(const authoring_navigation *index, int document)
{
    assert(document >= 0 && document < acount(index->documents));
    return index->texts[document];
}

source_text_kind authoring_navigation_document_kind(const authoring_navigation *index, int document)
{
    assert(document >= 0 && document < acount(index->documents));
    return index->text_kinds[document];
}

const authoring_reference *authoring_navigation_at(const authoring_navigation *index, int document, int offset)
{
    const authoring_reference *found = NULL;
    for (int i = 0; i < acount(index->references); i++) {
        const authoring_reference *reference = &index->references[i];
        script_range range = reference->location.range;
        if (reference->location.document == document && range.start <= offset &&
            offset < range.end &&
            (!found || range.end - range.start < found->location.range.end -
                                                     found->location.range.start)) {
            found = reference;
        }
    }
    return found;
}

const authoring_reference *authoring_navigation_definition(
    const authoring_navigation *index,
    authoring_target target
)
{
    const script_catalog_template
        *definition = authoring_navigation_template(index, target.id);
    if (definition) {
        target = (authoring_target){ definition->kind == SCRIPT_TEMPLATE_RECORD
                                         ? AUTHORING_TYPE
                                         : AUTHORING_CALLABLE,
                                     -1,
                                     definition->id,
                                     0 };
    }
    for (int i = 0; i < acount(index->references); i++) {
        const authoring_reference *reference = &index->references[i];
        if (reference->kind == AUTHORING_DEFINE &&
            authoring_target_equal(reference->target, target)) {
            return reference;
        }
    }
    return NULL;
}

int authoring_navigation_reference_count(const authoring_navigation *index)
{
    return acount(index->references);
}

const authoring_reference *authoring_navigation_reference_at(const authoring_navigation *index, int i)
{
    assert(i >= 0 && i < acount(index->references));
    return &index->references[i];
}

int authoring_navigation_call_count(const authoring_navigation *index)
{
    return acount(index->calls);
}

const authoring_call *authoring_navigation_call_at(const authoring_navigation *index, int i)
{
    assert(i >= 0 && i < acount(index->calls));
    return &index->calls[i];
}

authoring_target authoring_navigation_callable(const authoring_navigation *index, authoring_target target)
{
    if (target.kind == AUTHORING_CALLABLE) {
        return target;
    }
    const script_symbol *symbol = find_symbol(index, target);
    if (symbol && (symbol->kind == SCRIPT_ACTION || symbol->kind == SCRIPT_EVENT)) {
        return (authoring_target){ AUTHORING_CALLABLE, -1, symbol->capability, 0 };
    }
    return (authoring_target){ 0 };
}

authoring_target authoring_navigation_caller(const authoring_navigation *index, const authoring_call *call)
{
    int document = call->location.document;
    const script_analysis *analysis = index->documents[document].analysis;
    if (call->function < 0) {
        return program_target(analysis);
    }
    const script_function *function = &analysis->functions[call->function];
    if (function->symbol >= 0) {
        return symbol_target(document, &analysis->symbols[function->symbol]);
    }
    if (function->event) {
        return (authoring_target){ AUTHORING_CALLABLE, -1, function->event, 0 };
    }
    return program_target(analysis);
}

const char *authoring_navigation_name(const authoring_navigation *index, authoring_target target)
{
    const script_catalog_template
        *definition = authoring_navigation_template(index, target.id);
    if (definition) {
        const char *name = strrchr(definition->name, '.');
        return name ? name + 1 : definition->name;
    }
    if (target.kind == AUTHORING_PROGRAM) {
        for (int i = 0; i < acount(index->documents); i++) {
            const script_analysis *analysis = index->documents[i].analysis;
            if (!analysis) {
                continue;
            }
            if (authoring_target_equal(program_target(analysis), target)) {
                return analysis->name;
            }
        }
        return NULL;
    }
    const script_symbol *symbol = find_symbol(index, target);
    if (symbol) {
        return symbol->name;
    }
    if (target.kind == AUTHORING_CALLABLE || target.kind == AUTHORING_PROTOTYPE) {
        const script_catalog_capability
            *capability = script_catalog_capability_by_id(index->catalog, target.id);
        return capability ? capability->name : NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        index->catalog,
        target.kind == AUTHORING_FIELD ? target.owner : target.id
    );
    if (target.kind == AUTHORING_TYPE) {
        if (type) {
            return type->name;
        }
        for (int i = 0; i < index->catalog->relationship_count; i++) {
            if (index->catalog->relationships[i].id == target.id) {
                return index->catalog->relationships[i].name;
            }
        }
        return NULL;
    }
    if (target.kind == AUTHORING_FIELD && type) {
        for (int i = 0; i < type->field_count; i++) {
            if (type->fields[i].id == target.id) {
                return type->fields[i].name;
            }
        }
    }
    return NULL;
}
