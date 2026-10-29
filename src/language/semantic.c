#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* An await typed before a later assignment mixed its task state gives
 * results the state's tasks may not have. */
static void finish_joins(analyzer *a)
{
    for (int i = 0; i < acount(a->out->expressions); i++) {
        const script_expression *e = &a->out->expressions[i];
        if (e->kind != SCRIPT_EXPR_JOIN || !e->result_count ||
            a->out->expressions[e->left].kind != SCRIPT_EXPR_SYMBOL ||
            a->out->symbols[a->out->expressions[e->left].symbol].type.parameter ||
            !a->out->symbols[a->out->expressions[e->left].symbol].mixed_tasks) {
            continue;
        }
        script_diagnostic d = { .range = e->range };
        snprintf(d.message, sizeof d.message, "This task can hold tasks of other functions; await it only to wait");
        apush(a->out->diagnostics, d);
    }
}
static script_analysis *finish_analysis(analyzer *a, bool declarations, bool complete)
{
    script_analysis *out = a->out;
    if (!declarations) {
        for (int i = 0; i < acount(a->initializers); i++) {
            script_analyzer_resolve_initializer(a, a->initializers[i].symbol);
        }
        script_analyzer_finish_effects(a);
        script_analyzer_generic_check_callables(a);
        script_analyzer_finish_boxes(a);
        finish_joins(a);
        script_analyzer_finish_bindings(a);
        script_analyzer_check_constants(a);
    }
    for (int i = 0; i < acount(out->functions); i++) {
        if (out->functions[i].parent >= 0 &&
            (out->functions[i].kind == SCRIPT_FUNCTION_MACRO_ENTRY ||
             out->functions[i].kind == SCRIPT_FUNCTION_CONTINUATION)) {
            out->functions[i]
                .depth_limit = out->functions[out->functions[i].parent].depth_limit;
        }
        out->functions[i].parameter_count = acount(out->functions[i].parameters);
        out->functions[i].result_count = acount(out->functions[i].results);
    }
    out->symbol_count = acount(out->symbols);
    out->use_count = acount(out->uses);
    out->scope_count = acount(out->scopes);
    out->expression_count = acount(out->expressions);
    out->statement_count = acount(out->statements);
    out->function_count = acount(out->functions);
    out->query_count = acount(out->queries);
    out->description_count = acount(out->descriptions);
    out->binding_count = acount(out->bindings);
    out->diagnostic_count = acount(out->diagnostics);
    out->valid = !out->diagnostic_count;
    if (out->valid && !declarations && complete) {
        script_analyzer_units(a);
        out->diagnostic_count = acount(out->diagnostics);
        out->valid = !out->diagnostic_count;
    }
    if (out->valid && !declarations) {
        script_analysis_fold_constants(out);
    }
    out->warning_count = acount(out->warnings);
    afree(a->initializers);
    afree(a->bodies);
    afree(a->narrowed);
    afree(a->catalog_types);
    afree(a->catalog_capabilities);
    afree(a->imported_documents);
    afree(a->imported_imports);
    afree(a->environments);
    free(a->import_used);
    afree(a->constant_proofs);
    script_analyzer_generic_free(a);
    return out;
}
/* Systems always receive their current simulation step. */
static void system_step(analyzer *a, TSNode program, int function)
{
    (void)program;
    const script_catalog_capability
        *tick = script_catalog_capability_by_id(a->catalog, a->catalog->tick_event);
    if (!tick || tick->parameter_count != 1) {
        return;
    }
    const script_catalog_parameter *field = &tick->parameters[0];
    script_symbol step = { .id = script_catalog_id("system-step", a->out->identity),
                           .name = xstrdup("dt"),
                           .unit = field->unit ? xstrdup(field->unit) : NULL,
                           .kind = SCRIPT_PARAMETER,
                           .type = field->type,
                           .scope = a->scope,
                           .initializer = -1,
                           .function = function,
                           .declaration = { .start = -1, .end = -1 } };
    apush(a->out->symbols, step);
}
static script_analysis *analyze(
    const script_syntax *syntax,
    const script_catalog *catalog,
    bool declarations,
    const char *key,
    bool complete,
    const script_specialization_request *requests,
    int request_count
)
{
    assert(syntax && syntax->tree && catalog);
    script_syntax *recovered = script_analyzer_recovery_syntax(syntax);
    if (recovered) {
        script_analysis *out = analyze(recovered, catalog, declarations, key, false, requests, request_count);
        script_analyzer_recovery_restore(out, syntax);
        script_syntax_free(recovered);
        return out;
    }
    script_analysis *out = xcalloc(1, sizeof *out);
    out->text = xstrdup(syntax->text);
    out->package_scope = -1;
    out->query = -1;
    out->owner_symbol = -1;
    out->owner_type = catalog->entity_type
                        ? catalog->entity_type
                        : script_catalog_id("type", "ecs.entity");
    analyzer a = { .syntax = syntax,
                   .catalog = catalog,
                   .out = out,
                   .scope = -1,
                   .function = -1,
                   .package_scope = -1,
                   .declarations_only = declarations,
                   .requested = requests,
                   .requested_count = request_count };
    a.catalog_view = *catalog;
    for (int i = 0; i < catalog->type_count; i++) {
        apush(a.catalog_types, catalog->types[i]);
    }
    a.catalog_view.types = a.catalog_types;
    a.catalog = &a.catalog_view;
    script_analyzer_math_prepare(&a, ts_tree_root_node(syntax->tree));
    for (int i = 0; i < syntax->diagnostic_count; i++) {
        apush(out->diagnostics, syntax->diagnostics[i]);
    }
    TSNode root = ts_tree_root_node(syntax->tree);
    a.scope = script_analyzer_scope_new(&a, root);
    script_analyzer_host_declarations(&a, root);
    bool found = false;
    for (uint32_t i = 0; i < script_analyzer_named_count(root); i++) {
        TSNode program = ts_node_named_child(root, i);
        if (ts_node_is_extra(program)) {
            continue;
        }
        if (!script_analyzer_is(program, "behavior") &&
            !script_analyzer_is(program, "library") &&
            !script_analyzer_is(program, "system")) {
            script_analyzer_diagnostic(&a, program, "Expected behavior, library or system");
            continue;
        }
        if (found) {
            script_analyzer_diagnostic(&a, program, "One source file owns one program declaration");
            continue;
        }
        found = true;
        out->kind = script_analyzer_is(program, "system") ? SCRIPT_PROGRAM_SYSTEM
                  : script_analyzer_is(program, "library")
                      ? SCRIPT_PROGRAM_LIBRARY
                      : SCRIPT_PROGRAM_BEHAVIOR;
        out->name = script_analyzer_name_copy(&a, script_analyzer_field(program, "name"));
        out->identity = xstrdup(key ? key : out->name);
        TSNode body = script_analyzer_field(program, "body");
        a.scope = script_analyzer_scope_new(&a, body);
        int self = -1;
        {
            script_symbol owner = { .id = script_catalog_id("owner", out->identity),
                                    .name = xstrdup("self"),
                                    .kind = SCRIPT_GLOBAL,
                                    .type = { .storage = PIN_HANDLE,
                                              .nominal = out->owner_type },
                                    .scope = a.scope,
                                    .initializer = -1,
                                    .function = -1,
                                    .declaration = { .start = -1, .end = -1 } };
            apush(out->symbols, owner);
            self = acount(out->symbols) - 1;
            out->owner_symbol = self;
        }
        script_analyzer_declaration_pass(&a, body);
        int system = -1;
        if (out->kind == SCRIPT_PROGRAM_SYSTEM) {
            /* The step binds before any declaration reads it. */
            if (!declarations) {
                system = script_analyzer_function_new(&a, body, SCRIPT_FUNCTION_SYSTEM, -1);
                out->functions[system].scope = a.scope;
                out->functions[system].event = catalog->tick_event;
                system_step(&a, program, system);
            }
            script_analyzer_system_parameters(&a, program);
        }
        if (self >= 0) {
            out->symbols[self].type.nominal = out->owner_type;
        }
        if (declarations) {
            for (int j = 0; j < acount(a.initializers); j++) {
                int symbol = a.initializers[j].symbol;
                script_symbol_kind kind = out->symbols[symbol].kind;
                if (kind == SCRIPT_CONSTANT || kind == SCRIPT_PARAMETER ||
                    kind == SCRIPT_RESULT || kind == SCRIPT_STATE ||
                    kind == SCRIPT_SHARED_STATE) {
                    script_analyzer_resolve_initializer(&a, symbol);
                }
            }
        }
        if (!declarations) {
            script_analyzer_task_hints(&a, body);
            /* Inferred bodies first: other code reads their results. */
            script_analyzer_callable_definitions(&a, true);
            for (int j = 0; j < acount(a.initializers); j++) {
                script_analyzer_resolve_initializer(&a, a.initializers[j].symbol);
            }
            script_analyzer_definition_pass(&a, body);
            if (system >= 0) {
                script_analyzer_callable_definition(&a, system, body);
            }
            script_analyzer_callable_definitions(&a, false);
        }
        if (declarations && out->kind == SCRIPT_PROGRAM_LIBRARY) {
            /* Library signatures need inferred results; the full analysis
             * reports the bodies' diagnostics. */
            int reported = acount(out->diagnostics);
            script_analyzer_callable_definitions(&a, true);
            asetlen(out->diagnostics, reported);
        }
    }
    script_analyzer_generic_materialize(&a);
    if (!found) {
        script_analyzer_diagnostic(&a, root, "Source has no executable declaration");
    }
    return finish_analysis(&a, declarations, complete);
}

script_analysis *script_analyze(const script_syntax *syntax, const script_catalog *catalog)
{
    return analyze(syntax, catalog, false, NULL, true, NULL, 0);
}
script_analysis *script_analyze_document(
    const script_syntax *syntax,
    const script_catalog *catalog,
    const char *key
)
{
    assert(key && key[0]);
    return analyze(syntax, catalog, false, key, true, NULL, 0);
}
script_analysis *script_analyze_document_requested(
    const script_syntax *syntax,
    const script_catalog *catalog,
    const char *key,
    const script_specialization_request *requests,
    int count
)
{
    assert(key && key[0] && count >= 0 && (!count || requests));
    return analyze(syntax, catalog, false, key, true, requests, count);
}
script_analysis *script_declarations(const script_syntax *syntax, const script_catalog *catalog)
{
    return analyze(syntax, catalog, true, NULL, true, NULL, 0);
}
script_analysis *script_declarations_document(
    const script_syntax *syntax,
    const script_catalog *catalog,
    const char *key
)
{
    assert(key && key[0]);
    return analyze(syntax, catalog, true, key, true, NULL, 0);
}
const script_source_document *script_analysis_document(const script_analysis *out, int document)
{
    return out && document >= 0 && document < out->document_count
             ? &out->documents[document]
             : NULL;
}
static bool package_declaration(TSNode node)
{
    return script_analyzer_is(node, "function_declaration") ||
           script_analyzer_is(node, "constant_declaration") ||
           script_analyzer_is(node, "record_declaration") ||
           script_analyzer_is(node, "union_declaration");
}
/* Marks the declarations that `pub` exports to importing packages. */
static void mark_public(analyzer *a, TSNode root)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(root); i++) {
        TSNode node = ts_node_named_child(root, i);
        if (ts_node_is_null(script_analyzer_field(node, "visibility"))) {
            continue;
        }
        script_range name = script_analyzer_range(a, script_analyzer_field(node, "name"));
        for (int j = 0; j < acount(a->out->symbols); j++) {
            script_symbol *symbol = &a->out->symbols[j];
            if (symbol->scope == a->package_scope &&
                symbol->declaration.document == name.document &&
                symbol->declaration.start == name.start) {
                symbol->pub = true;
            }
        }
    }
}
static script_analysis *analyze_package(const script_package_options *options, bool declarations)
{
    assert(options);
    const script_package_document *documents = options->documents;
    int count = options->document_count;
    const script_catalog *catalog = options->native;
    const char *identity = options->identity;
    assert(catalog && identity && identity[0]);
    script_analysis *out = xcalloc(1, sizeof *out);
    out->identity = xstrdup(identity);
    out->name = xstrdup("");
    out->text = xstrdup("");
    out->kind = SCRIPT_PROGRAM_LIBRARY;
    out->package_scope = -1;
    out->query = -1;
    out->owner_symbol = -1;
    out->owner_type = catalog->entity_type
                        ? catalog->entity_type
                        : script_catalog_id("type", "ecs.entity");
    analyzer a = { .catalog = catalog,
                   .native = catalog,
                   .imports = options->imports,
                   .import_count = options->import_count,
                   .requested = options->requests,
                   .requested_count = options->request_count,
                   .out = out,
                   .scope = -1,
                   .function = -1,
                   .package_scope = -1,
                   .declarations_only = declarations };
    if (count < 1 || count > SCRIPT_PACKAGE_MAX_DOCUMENTS) {
        script_diagnostic error = { .range = { .line = 1, .column = 1 } };
        snprintf(error.message, sizeof error.message, "A package needs between 1 and %d documents", SCRIPT_PACKAGE_MAX_DOCUMENTS);
        apush(out->diagnostics, error);
        return finish_analysis(&a, true, false);
    }
    assert(documents);
    a.documents = documents;
    a.document_count = count;
    out->documents = xcalloc((size_t)count, sizeof *out->documents);
    out->document_count = count;
    out->package_document_count = count;
    for (int i = 0; i < count; i++) {
        const script_package_document *document = &documents[i];
        assert(
            document->path && document->path[0] && document->key &&
            document->key[0] && document->syntax && document->syntax->tree
        );
        out->documents[i] = (script_source_document){
            .path = xstrdup(document->path),
            .key = xstrdup(document->key),
            .text = xstrdup(document->syntax->text),
        };
    }
    free(out->text);
    out->text = xstrdup(documents[0].syntax->text);
    a.syntax = documents[0].syntax;
    a.catalog_view = *catalog;
    for (int i = 0; i < catalog->type_count; i++) {
        apush(a.catalog_types, catalog->types[i]);
    }
    a.catalog_view.types = a.catalog_types;
    a.catalog = &a.catalog_view;
    for (int i = 0; i < count; i++) {
        script_analyzer_math_prepare(&a, ts_tree_root_node(documents[i].syntax->tree));
    }
    if (!script_analyzer_package_metadata(&a)) {
        return finish_analysis(&a, true, false);
    }
    TSNode first_header = { 0 };
    for (int i = 0; i < count; i++) {
        const script_syntax *syntax = documents[i].syntax;
        TSNode root = ts_tree_root_node(syntax->tree);
        for (int j = 0; j < syntax->diagnostic_count; j++) {
            script_diagnostic error = syntax->diagnostics[j];
            error.range.document = i;
            apush(out->diagnostics, error);
        }
        if (syntax->diagnostic_count) {
            continue;
        }
        TSNode header = script_analyzer_field(root, "package");
        if (ts_node_is_null(header)) {
            script_analyzer_diagnostic(&a, root, "Expected package header");
            continue;
        }
        TSNode name_node = script_analyzer_field(header, "name");
        char *name = script_analyzer_name_copy(&a, name_node);
        if (!strcmp(name, "_")) {
            script_analyzer_diagnostic(&a, name_node, "A package needs a non-blank name");
        }
        if (ts_node_is_null(first_header)) {
            free(out->name);
            out->name = xstrdup(name);
            first_header = name_node;
        } else if (strcmp(out->name, name)) {
            script_analyzer_diagnostic(&a, name_node, "Package header differs from sibling '%s'", out->name);
            script_analyzer_diagnostic(&a, first_header, "Previous package header is here");
        }
        free(name);
        for (int j = 0; j < i; j++) {
            if (!strcmp(documents[i].path, documents[j].path) ||
                !strcmp(documents[i].key, documents[j].key)) {
                script_analyzer_diagnostic(&a, name_node, "Duplicate package document path or key");
            }
        }
        for (uint32_t j = 0; j < script_analyzer_named_count(root); j++) {
            TSNode node = ts_node_named_child(root, j);
            if (ts_node_is_extra(node) || script_analyzer_is(node, "package_header")) {
                continue;
            }
            if (script_analyzer_is(node, "import_declaration")) {
                script_analyzer_package_import(&a, node);
            } else if (!package_declaration(node)) {
                script_analyzer_diagnostic(&a, node, "Package execution currently supports functions, constants, records and unions");
            }
        }
    }
    script_analyzer_package_imports_check(&a, false);
    if (acount(out->diagnostics)) {
        return finish_analysis(&a, true, false);
    }
    TSNode root = ts_tree_root_node(documents[0].syntax->tree);
    a.scope = script_analyzer_scope_new(&a, root);
    script_analyzer_host_declarations(&a, root);
    a.scope = script_analyzer_scope_new(&a, root);
    a.package_scope = a.scope;
    out->package_scope = a.package_scope;
    /* Register all nominal identities before fields or callable signatures. */
    for (int i = 0; i < count; i++) {
        script_analyzer_record_types(&a, ts_tree_root_node(documents[i].syntax->tree));
    }
    for (int i = 0; i < count; i++) {
        script_analyzer_union_types(&a, ts_tree_root_node(documents[i].syntax->tree));
    }
    for (int i = 0; i < count; i++) {
        script_analyzer_union_fields(&a, ts_tree_root_node(documents[i].syntax->tree));
    }
    for (int i = 0; i < count; i++) {
        script_analyzer_value_declarations(
            &a,
            ts_tree_root_node(documents[i].syntax->tree)
        );
    }
    for (int i = 0; i < count; i++) {
        mark_public(&a, ts_tree_root_node(documents[i].syntax->tree));
    }
    script_analyzer_package_imports_check(&a, true);
    /* Record defaults can name any sibling constant. Register every value
     * first, matching the single-document declaration pass. */
    for (int i = 0; i < count; i++) {
        script_analyzer_record_fields(&a, ts_tree_root_node(documents[i].syntax->tree));
    }
    if (!acount(out->diagnostics)) {
        script_analyzer_record_layouts(&a, root);
    }
    if (acount(out->diagnostics)) {
        return finish_analysis(&a, true, false);
    }
    if (!declarations) {
        script_analyzer_callable_definitions(&a, true);
    }
    for (int i = 0; i < acount(a.initializers); i++) {
        script_analyzer_resolve_initializer(&a, a.initializers[i].symbol);
    }
    if (declarations) {
        int reported = acount(out->diagnostics);
        script_analyzer_callable_definitions(&a, true);
        asetlen(out->diagnostics, reported);
    } else {
        script_analyzer_callable_definitions(&a, false);
    }
    if (!declarations) {
        script_analyzer_generic_materialize(&a);
    }
    return finish_analysis(&a, declarations, true);
}
script_analysis *script_analyze_package(const script_package_options *options)
{
    return analyze_package(options, false);
}
script_analysis *script_declarations_package(const script_package_options *options)
{
    return analyze_package(options, true);
}
void script_analysis_free(script_analysis *out)
{
    if (!out) {
        return;
    }
    free(out->text);
    free(out->name);
    free(out->identity);
    for (int i = 0; i < out->document_count; i++) {
        free(out->documents[i].path);
        free(out->documents[i].key);
        free(out->documents[i].text);
    }
    free(out->documents);
    for (int i = 0; i < acount(out->symbols); i++) {
        free(out->symbols[i].name);
        free(out->symbols[i].unit);
    }
    for (int i = 0; i < acount(out->expressions); i++) {
        script_expression *expression = &out->expressions[i];
        if (expression->kind == SCRIPT_EXPR_CONSTANT && !expression->constant_snapshot &&
            expression->constant.container == PIN_CONTAINER_SINGLE &&
            (expression->constant.type == PIN_STR ||
             expression->constant.type == PIN_NAME)) {
            free(
                expression->constant.type == PIN_STR
                    ? (void *)expression->constant.text
                    : (void *)expression->constant.str
            );
        }
        afree(expression->arguments);
        afree(expression->results);
        afree(expression->pattern_tags);
    }
    for (int i = 0; i < acount(out->statements); i++) {
        afree(out->statements[i].children);
    }
    for (int i = 0; i < acount(out->functions); i++) {
        afree(out->functions[i].parameters);
        afree(out->functions[i].results);
        free(out->functions[i].entry);
    }
    for (int i = 0; i < acount(out->queries); i++) {
        afree(out->queries[i].children);
    }
    for (int i = 0; i < acount(out->descriptions); i++) {
        afree(out->descriptions[i].symbols);
        afree(out->descriptions[i].captures);
        afree(out->descriptions[i].stages);
    }
    afree(out->descriptions);
    for (int i = 0; i < acount(out->bindings); i++) {
        afree(out->bindings[i].reads);
        afree(out->bindings[i].writes);
    }
    afree(out->symbols);
    afree(out->uses);
    afree(out->scopes);
    afree(out->expressions);
    afree(out->statements);
    afree(out->functions);
    afree(out->queries);
    afree(out->bindings);
    afree(out->types);
    afree(out->templates);
    afree(out->requests);
    afree(out->specializations);
    afree(out->diagnostics);
    afree(out->warnings);
    arena_free(&out->constants);
    free(out);
}
const script_use *script_use_at(const script_analysis *out, int offset)
{
    const script_use *best = NULL;
    for (int i = 0; i < out->use_count; i++) {
        const script_use *use = &out->uses[i];
        if (!use->range.document && offset >= use->range.start &&
            offset < use->range.end &&
            (!best || use->range.end - use->range.start <
                          best->range.end - best->range.start)) {
            best = use;
        }
    }
    return best;
}
const script_symbol *script_symbol_at(const script_analysis *out, int offset)
{
    const script_use *use = script_use_at(out, offset);
    if (use && use->symbol >= 0) {
        return &out->symbols[use->symbol];
    }
    for (int i = 0; i < out->symbol_count; i++) {
        if (!out->symbols[i].declaration.document &&
            offset >= out->symbols[i].declaration.start &&
            offset < out->symbols[i].declaration.end) {
            return &out->symbols[i];
        }
    }
    return NULL;
}
