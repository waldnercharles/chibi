#include "authoring/source_edit_internal.h"
#include "authoring/source_literal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
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
static const source_presentation_origin *origin(const authoring_source_presentation *p, uint64_t id)
{
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->identity == id) {
            return o;
        }
    }
    return NULL;
}
static void patch(source_edit_changes *changes, script_range range, const char *text)
{
    for (int i = 0; i < acount(changes->patches); i++) {
        if (changes->patches[i].start == range.start &&
            changes->patches[i].end == range.end) {
            return;
        }
    }
    apush(changes->patches, ((text_edit){ range.start, range.end, xstrdup(text) }));
}
static int symbol(const script_analysis *a, const source_presentation_origin *o)
{
    if (!o) {
        return -1;
    }
    if (o->kind == SOURCE_PRESENTATION_SYMBOL) {
        return o->record;
    }
    if (o->kind == SOURCE_PRESENTATION_FUNCTION && o->record >= 0 &&
        o->record < a->function_count) {
        return a->functions[o->record].symbol;
    }
    if (o->kind == SOURCE_PRESENTATION_STATEMENT && o->record >= 0 &&
        o->record < a->statement_count) {
        return a->statements[o->record].symbol;
    }
    return -1;
}
static bool identifier(const char *name)
{
    if (!name || !(isalpha((unsigned char)*name) || *name == '_')) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)name + 1; *p; p++) {
        if (!(isalnum(*p) || *p == '_')) {
            return false;
        }
    }
    return true;
}

static bool variable_type(const script_catalog *catalog, const char *text, script_type *type)
{
    if (!text || !text[0] || strlen(text) > 4096) {
        return false;
    }
    const char *prefix = "__VariableType :: behavior { __probe :: (__value: ";
    const char *suffix = ") {} }";
    size_t bytes = strlen(prefix) + strlen(text) + strlen(suffix) + 1;
    char *probe = xmalloc(bytes);
    snprintf(probe, bytes, "%s%s%s", prefix, text, suffix);
    script_syntax *syntax = script_parse(probe);
    script_analysis *analysis = script_analyze(syntax, catalog);
    bool valid = false;
    if (!syntax->diagnostic_count && analysis->valid) {
        for (int i = 0; i < analysis->symbol_count; i++) {
            const script_symbol *symbol = &analysis->symbols[i];
            if (symbol->kind != SCRIPT_PARAMETER || strcmp(symbol->name, "__value")) {
                continue;
            }
            TSNode node = ts_node_named_descendant_for_byte_range(
                ts_tree_root_node(syntax->tree),
                (uint32_t)symbol->declaration.start,
                (uint32_t)symbol->declaration.end
            );
            while (!ts_node_is_null(node) && strcmp(ts_node_type(node), "parameter")) {
                node = ts_node_parent(node);
            }
            TSNode declared = ts_node_child_by_field_name(node, "type", 4);
            valid = !ts_node_is_null(declared) &&
                    ts_node_start_byte(declared) == strlen(prefix) &&
                    ts_node_end_byte(declared) == strlen(prefix) + strlen(text);
            *type = symbol->type;
            break;
        }
    }
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    free(probe);
    return valid;
}

static bool retype_variable(
    const script_analysis *a,
    const script_catalog *catalog,
    const script_symbol *selected,
    const authoring_edit_request *r,
    source_edit_changes *changes,
    char *error,
    size_t size
)
{
    script_type type;
    if ((selected->kind != SCRIPT_STATE && selected->kind != SCRIPT_SHARED_STATE) ||
        !variable_type(catalog, r->signature, &type)) {
        return fail(error, size, "Choose one valid variable type");
    }
    script_syntax *syntax = script_parse(a->text);
    TSNode declaration = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(syntax->tree),
        (uint32_t)selected->declaration.start,
        (uint32_t)selected->declaration.end
    );
    while (!ts_node_is_null(declaration) &&
           strcmp(ts_node_type(declaration), "state_declaration")) {
        declaration = ts_node_parent(declaration);
    }
    assert(!ts_node_is_null(declaration));
    TSNode old_type = ts_node_child_by_field_name(declaration, "type", 4);
    TSNode value = ts_node_child_by_field_name(declaration, "value", 5);
    bool keep = selected->initializer >= 0 &&
                script_type_assignable(
                    type,
                    a->expressions[selected->initializer].type
                );
    const script_catalog_type *named = script_catalog_type_by_id(catalog, type.nominal);
    pin_value zero = { .type = type.storage,
                       .container = type.container,
                       .type_name = named ? named->name : NULL };
    if (type.storage == PIN_NAME) {
        zero.str = "";
    }
    char *literal = keep ? NULL
                         : authoring_source_literal(catalog, type, &zero, NULL, 0);
    TSNode assign = { 0 };
    for (uint32_t i = 0; i < ts_node_child_count(declaration); i++) {
        TSNode child = ts_node_child(declaration, i);
        if (!strcmp(ts_node_type(child), "=") || !strcmp(ts_node_type(child), ":=")) {
            assign = child;
            break;
        }
    }
    if (!ts_node_is_null(old_type)) {
        patch(changes, script_node_range(old_type), r->signature);
        if (ts_node_is_null(value) && literal) {
            size_t bytes = strlen(literal) + 4;
            char *initial = xmalloc(bytes);
            snprintf(initial, bytes, " = %s", literal);
            int end = (int)ts_node_end_byte(old_type);
            patch(changes, (script_range){ .start = end, .end = end }, initial);
            free(initial);
        }
        if (!ts_node_is_null(value) && !keep && !literal) {
            assert(!ts_node_is_null(assign));
            patch(changes, script_node_range(assign), "");
        }
    } else {
        assert(!ts_node_is_null(assign));
        size_t bytes = strlen(r->signature) + 6;
        char *annotation = xmalloc(bytes);
        bool retained = !ts_node_is_null(
            ts_node_child_by_field_name(declaration, "storage", 7)
        );
        snprintf(annotation, bytes, "%s%s%s", retained ? " " : ": ", r->signature, keep || literal ? " =" : "");
        patch(changes, script_node_range(assign), annotation);
        free(annotation);
    }
    if (!ts_node_is_null(value) && !keep) {
        patch(changes, script_node_range(value), literal ? literal : "");
    }
    free(literal);
    script_syntax_free(syntax);
    changes->text = text_edits_apply(a->text, changes->patches, acount(changes->patches));
    return changes->text != NULL || fail(error, size, "Variable type edits overlap");
}
static int mapped(int offset, const text_edit *patches)
{
    int result = offset;
    for (int i = 0; i < acount(patches); i++) {
        if (patches[i].end <= offset) {
            result += (int)strlen(patches[i].text) -
                      (patches[i].end - patches[i].start);
        }
    }
    return result;
}
static bool rename_preserves_uses(
    const script_analysis *a,
    const script_catalog *c,
    const source_edit_changes *changes,
    int selected
)
{
    script_syntax *syntax = script_parse(changes->text);
    script_analysis *next = script_analyze_document(syntax, c, a->identity);
    /* A rename changes tokens, not declaration ordering. Compare every use,
     * including untouched ones that the new name could capture. */
    bool valid = next->valid && next->symbol_count == a->symbol_count &&
                 next->use_count == a->use_count;
    for (int i = 0; valid && i < a->symbol_count; i++) {
        valid = a->symbols[i].kind == next->symbols[i].kind &&
                (i == selected || !strcmp(a->symbols[i].name, next->symbols[i].name));
    }
    for (int i = 0; valid && i < a->use_count; i++) {
        const script_use *old = &a->uses[i];
        script_range range = { .start = mapped(old->range.start, changes->patches),
                               .end = mapped(old->range.end, changes->patches) };
        valid = false;
        for (int j = 0; j < next->use_count; j++) {
            const script_use *use = &next->uses[j];
            if (use->range.start == range.start && use->range.end == range.end &&
                use->kind == old->kind && use->symbol == old->symbol &&
                (old->symbol == selected || use->member == old->member)) {
                valid = true;
                break;
            }
        }
    }
    script_analysis_free(next);
    script_syntax_free(syntax);
    return valid;
}
bool source_edit_declarations(
    const script_analysis *a,
    const script_catalog *c,
    const authoring_source_presentation *p,
    const authoring_edit_request *r,
    source_edit_changes *changes,
    char *error,
    size_t size
)
{
    if (r->kind == AUTHORING_EDIT_NAME_VALUE && r->producer_result > 0) {
        return fail(error, size, "Name the selected tuple field through its authored result declaration");
    }

    if (r->kind == AUTHORING_EDIT_CREATE_FUNCTION ||
        r->kind == AUTHORING_EDIT_CREATE_MACRO ||
        r->kind == AUTHORING_EDIT_CREATE_EXPRESSION) {
        const char *name = r->name && r->name[0] ? r->name : "NewValue";
        if (strlen(name) > 256 || !identifier(name)) {
            return fail(error, size, "Declaration names must be identifiers within 256 bytes");
        }
        char *quoted = authoring_source_identifier(name, error, size);
        if (!quoted) {
            return false;
        }
        char *fragment;
        if (r->kind == AUTHORING_EDIT_CREATE_EXPRESSION) {
            if (!r->expression || strlen(r->expression) > 64 * 1024 ||
                !source_edit_single_expression(r->expression)) {
                free(quoted);
                return fail(error, size, "Create a single expression within the 64 KiB limit");
            }
            /* A named expression is a function, recomputed at each call. */
            size_t bytes = strlen(quoted) + strlen(r->expression) + 32;
            fragment = xmalloc(bytes);
            snprintf(fragment, bytes, "%s :: () { return %s }", quoted, r->expression);
        } else {
            size_t bytes = strlen(quoted) + 64;
            fragment = xmalloc(bytes);
            snprintf(
                fragment,
                bytes,
                r->kind == AUTHORING_EDIT_CREATE_MACRO
                    ? "macro %s()->() { entry Start {} }"
                    : "%s :: () {}",
                quoted
            );
        }
        authoring_edit_request insertion = *r;
        insertion.kind = AUTHORING_EDIT_APPEND_DECLARATIONS;
        insertion.fragment = fragment;
        bool ok = source_edit_structural(a, c, p, &insertion, changes, error, size);
        free(fragment);
        free(quoted);
        return ok;
    }
    const source_presentation_origin *o = origin(p, r->origin);
    int index = symbol(a, o);
    if (r->kind == AUTHORING_EDIT_SET_DISABLED && o &&
        o->kind == SOURCE_PRESENTATION_EXPRESSION && o->record >= 0 &&
        o->record < a->expression_count) {
        int owner = a->expressions[o->record].symbol;
        if (owner >= 0 && owner < a->symbol_count &&
            a->symbols[owner].kind == SCRIPT_ACTION) {
            index = owner;
        }
    }
    if ((r->kind == AUTHORING_EDIT_NAME_VALUE || r->kind == AUTHORING_EDIT_SET_DISABLED) &&
        o && o->kind == SOURCE_PRESENTATION_EXPRESSION && o->record >= 0 &&
        o->record < a->expression_count) {
        for (int i = 0; i < a->symbol_count; i++) {
            if (a->symbols[i].initializer == o->record &&
                (a->symbols[i].kind == SCRIPT_LOCAL || a->symbols[i].kind == SCRIPT_ACTION ||
                 a->symbols[i].kind == SCRIPT_STATE)) {
                index = i;
                break;
            }
        }
        if (index < 0 && r->kind == AUTHORING_EDIT_NAME_VALUE) {
            if (!r->name || !r->name[0] || strlen(r->name) > 256) {
                return fail(error, size, "Provide a nonempty retained value name");
            }
            for (int i = 0; i < a->symbol_count; i++) {
                if (!strcmp(a->symbols[i].name, r->name)) {
                    return fail(error, size, "This retained value name already belongs to a declaration");
                }
            }
            const script_statement *statement = NULL;
            for (int i = 0; i < a->statement_count; i++) {
                const script_statement *candidate = &a->statements[i];
                if (candidate->kind == SCRIPT_STMT_ASSIGN &&
                    candidate->second == o->record && candidate->expression >= 0 &&
                    a->expressions[candidate->expression].kind == SCRIPT_EXPR_SYMBOL) {
                    statement = candidate;
                    break;
                }
            }
            if (!statement) {
                return fail(error, size, "Name this result in its declaration or as a complete assignment value");
            }
            if (!identifier(r->name)) {
                return fail(error, size, "Retained value references require an identifier name");
            }
            char *name = xstrdup(r->name);
            size_t bytes = strlen(name) + (size_t)(o->range.end - o->range.start) + 16;
            char *declaration = xmalloc(bytes);
            snprintf(
                declaration,
                bytes,
                "%s := %.*s;\n",
                name,
                o->range.end - o->range.start,
                a->text + o->range.start
            );
            script_syntax *syntax = script_parse(a->text);
            script_range owner = source_edit_whole_statement(syntax, statement->range);
            patch(changes, (script_range){ .start = owner.start, .end = owner.start }, declaration);
            patch(changes, o->range, name);
            script_syntax_free(syntax);
            free(declaration);
            free(name);
            changes->text = text_edits_apply(
                a->text,
                changes->patches,
                acount(changes->patches)
            );
            if (!changes->text) {
                source_edit_changes_free(changes);
                return fail(error, size, "Retained value naming ranges overlap");
            }
            return true;
        }
    }
    if (index < 0 || index >= a->symbol_count) {
        return fail(error, size, "Select an owned declaration for this operation");
    }
    const script_symbol *selected = &a->symbols[index];
    if (r->kind == AUTHORING_EDIT_RETYPE_VARIABLE) {
        return retype_variable(a, c, selected, r, changes, error, size);
    }
    if (selected->kind == SCRIPT_GLOBAL || selected->kind == SCRIPT_COMPONENT) {
        return fail(error, size, "Native declarations are edited in their authoritative catalog");
    }
    if (r->kind == AUTHORING_EDIT_RENAME_DECLARATION ||
        r->kind == AUTHORING_EDIT_NAME_VALUE) {
        if (!r->name || !r->name[0] || strlen(r->name) > 256) {
            return fail(error, size, "Provide a nonempty declaration name within 256 bytes");
        }
        if (!identifier(r->name)) {
            return fail(error, size, "Retained value references require an identifier name");
        }
        patch(changes, selected->declaration, r->name);
        script_syntax *syntax = script_parse(a->text);
        for (int i = 0; i < a->use_count; i++) {
            const script_use *use = &a->uses[i];
            if (use->symbol != index || (use->member && use->kind != SCRIPT_CALL)) {
                continue;
            }
            script_range range = use->range;
            if ((selected->kind == SCRIPT_STATE || selected->kind == SCRIPT_SHARED_STATE) &&
                use->kind == SCRIPT_WRITE) {
                TSNode node = ts_node_named_descendant_for_byte_range(
                    ts_tree_root_node(syntax->tree),
                    (uint32_t)range.start,
                    (uint32_t)range.end
                );
                if (!strcmp(ts_node_type(node), "member_expression")) {
                    TSNode member = ts_node_child_by_field_name(node, "member", 6);
                    range = script_node_range(member);
                }
            }
            patch(changes, range, r->name);
        }
        script_syntax_free(syntax);
    } else if (r->kind == AUTHORING_EDIT_SET_DISABLED) {
        script_syntax *syntax = script_parse(a->text);
        TSNode node = ts_node_named_descendant_for_byte_range(
            ts_tree_root_node(syntax->tree),
            (uint32_t)selected->declaration.start,
            (uint32_t)selected->declaration.end
        );
        while (!ts_node_is_null(node) &&
               strcmp(ts_node_type(node), "action_declaration")) {
            node = ts_node_parent(node);
        }
        if (ts_node_is_null(node)) {
            script_syntax_free(syntax);
            return fail(error, size, "Only action declarations support disabled source syntax");
        }
        TSNode first = ts_node_child(node, 0);
        bool disabled = !strcmp(ts_node_type(first), "disabled");
        if (r->disabled && !disabled) {
            int start = (int)ts_node_start_byte(node);
            patch(changes, (script_range){ .start = start, .end = start }, "disabled ");
        } else if (!r->disabled && disabled) {
            patch(changes, script_node_range(first), "");
        }
        script_syntax_free(syntax);
    } else {
        return fail(error, size, "This declaration operation is not represented by the language");
    }
    changes->text = text_edits_apply(a->text, changes->patches, acount(changes->patches));
    if (!changes->text) {
        source_edit_changes_free(changes);
        return fail(error, size, "Declaration edits overlap");
    }
    if ((r->kind == AUTHORING_EDIT_RENAME_DECLARATION ||
         r->kind == AUTHORING_EDIT_NAME_VALUE) &&
        !rename_preserves_uses(a, c, changes, index)) {
        source_edit_changes_free(changes);
        return fail(error, size, "Renaming would capture or change a resolved reference");
    }
    return true;
}
