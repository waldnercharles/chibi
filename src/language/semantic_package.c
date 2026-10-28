#include "capabilities/catalog_snapshot.h"
#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void import_error(analyzer *a, const script_package_import *import, const char *message)
{
    script_diagnostic error = { .range = import->range };
    error.range.document = import->document;
    snprintf(error.message, sizeof error.message, "%s", message);
    apush(a->out->diagnostics, error);
}
static bool identifier(const char *name)
{
    if (!name || !(isalpha((unsigned char)*name) || *name == '_')) {
        return false;
    }
    for (name++; *name; name++) {
        if (!(isalnum((unsigned char)*name) || *name == '_')) {
            return false;
        }
    }
    return true;
}
static bool valid_exports(const script_package_exports *exports)
{
    if (!exports->identity || !exports->metadata || exports->member_count < 0 ||
        exports->member_count > 4096 ||
        (exports->member_count && !exports->members) || exports->private_count < 0 ||
        (exports->private_count && !exports->private_names)) {
        return false;
    }
    const script_catalog *metadata = exports->metadata;
    if (metadata->type_count < 0 || (metadata->type_count && !metadata->types) ||
        metadata->capability_count < 0 ||
        (metadata->capability_count && !metadata->capabilities)) {
        return false;
    }
    for (int i = 0; i < exports->member_count; i++) {
        const script_package_export *member = &exports->members[i];
        if (!identifier(member->name) || !member->id ||
            member->kind < SCRIPT_PACKAGE_FUNCTION ||
            member->kind > SCRIPT_PACKAGE_CONSTANT) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(member->name, exports->members[j].name)) {
                return false;
            }
        }
        if (member->kind == SCRIPT_PACKAGE_TYPE &&
            !script_catalog_type_by_id(metadata, member->id)) {
            return false;
        }
        if (member->kind == SCRIPT_PACKAGE_FUNCTION) {
            const script_catalog_capability
                *function = script_catalog_capability_by_id(metadata, member->id);
            if (!function || function->kind != SCRIPT_CAPABILITY_FUNCTION) {
                return false;
            }
        }
    }
    return true;
}
static int type_index(const analyzer *a, uint64_t id)
{
    for (int i = 0; i < a->catalog->type_count; i++) {
        if (a->catalog->types[i].id == id) {
            return i;
        }
    }
    return -1;
}
static int capability_index(const analyzer *a, uint64_t id)
{
    for (int i = 0; i < a->catalog->capability_count; i++) {
        if (a->catalog->capabilities[i].id == id) {
            return i;
        }
    }
    return -1;
}
bool script_analyzer_package_metadata(analyzer *a)
{
    if (a->import_count < 0 || a->import_count > 4096 ||
        (a->import_count && !a->imports)) {
        script_analyzer_diagnostic(a, ts_tree_root_node(a->syntax->tree), "Invalid package import bindings");
        return false;
    }
    a->import_used = xcalloc((size_t)a->import_count, sizeof *a->import_used);
    for (int i = 0; i < a->native->capability_count; i++) {
        apush(a->catalog_capabilities, a->native->capabilities[i]);
    }
    a->catalog_view.capabilities = a->catalog_capabilities;
    a->catalog_view.capability_count = acount(a->catalog_capabilities);
    for (int i = 0; i < a->import_count; i++) {
        const script_package_import *import = &a->imports[i];
        bool self = import->identity && !strcmp(import->identity, a->out->identity);
        bool blank = import->alias && !strcmp(import->alias, "_");
        if (import->document < 0 || import->document >= a->document_count ||
            !identifier(import->alias) || !import->identity ||
            !import->identity[0] || (!self && !blank && !import->exports) ||
            (import->exports && (!valid_exports(import->exports) ||
                                 strcmp(import->identity, import->exports->identity)))) {
            import_error(a, import, "Invalid package import binding");
            continue;
        }
        bool retained_before = false;
        for (int j = 0; j < i; j++) {
            retained_before |= import->exports &&
                               import->exports == a->imports[j].exports &&
                               a->imports[j].alias &&
                               strcmp(a->imports[j].alias, "_") &&
                               a->imports[j].identity &&
                               strcmp(a->imports[j].identity, a->out->identity);
            if (!blank && a->imports[j].document == import->document &&
                a->imports[j].alias && !strcmp(a->imports[j].alias, import->alias)) {
                import_error(a, import, "Duplicate package import alias");
            }
            if (import->exports && a->imports[j].exports && a->imports[j].identity &&
                !strcmp(import->identity, a->imports[j].identity) &&
                import->exports != a->imports[j].exports) {
                import_error(a, import, "Package imports select different export generations");
            }
        }
        if (!import->exports || blank || self || retained_before) {
            continue;
        }
        script_catalog retained;
        if (!script_catalog_snapshot(&a->out->constants, import->exports->metadata, &retained)) {
            import_error(a, import, "Cannot retain imported package metadata");
            continue;
        }
        const script_catalog *metadata = &retained;
        for (int j = 0; j < metadata->type_count; j++) {
            const script_catalog_type *type = &metadata->types[j];
            if (type_index(a, type->id) < 0) {
                apush(a->catalog_types, *type);
                a->catalog_view.types = a->catalog_types;
                a->catalog_view.type_count = acount(a->catalog_types);
            }
        }
        for (int j = 0; j < metadata->capability_count; j++) {
            const script_catalog_capability *capability = &metadata->capabilities[j];
            if (capability_index(a, capability->id) < 0) {
                apush(a->catalog_capabilities, *capability);
                a->catalog_view.capabilities = a->catalog_capabilities;
                a->catalog_view.capability_count = acount(a->catalog_capabilities);
            }
        }
    }
    return !acount(a->out->diagnostics);
}
bool script_analyzer_package_import(analyzer *a, TSNode node)
{
    script_range range = script_analyzer_range(a, node);
    TSNode path = script_analyzer_field(node, "path");
    if (!ts_node_is_null(path)) {
        range.end = script_analyzer_range(a, path).end;
    }
    const script_package_import *binding = NULL;
    for (int i = 0; i < a->import_count; i++) {
        const script_package_import *candidate = &a->imports[i];
        if (candidate->document == range.document &&
            candidate->range.start == range.start &&
            candidate->range.end == range.end) {
            if (binding) {
                script_analyzer_diagnostic(a, node, "Duplicate package import binding");
                return false;
            }
            binding = candidate;
            a->import_used[i] = true;
        }
    }
    if (!binding) {
        script_analyzer_diagnostic(a, node, "Package imports require package resolution");
        return false;
    }
    TSNode alias = script_analyzer_field(node, "alias");
    if (!ts_node_is_null(alias)) {
        char *name = script_analyzer_name_copy(a, alias);
        bool equal = !strcmp(name, binding->alias);
        free(name);
        if (!equal) {
            script_analyzer_diagnostic(a, alias, "Package import alias differs from its binding");
            return false;
        }
    }
    return true;
}
void script_analyzer_package_imports_check(analyzer *a, bool declarations)
{
    for (int i = 0; i < a->import_count; i++) {
        const script_package_import *import = &a->imports[i];
        if (!declarations) {
            if (!a->import_used[i]) {
                import_error(a, import, "Package import binding has no source declaration");
            }
            continue;
        }
        if (!strcmp(import->alias, "_")) {
            continue;
        }
        for (int j = 0; j < acount(a->out->symbols); j++) {
            const script_symbol *symbol = &a->out->symbols[j];
            if (symbol->scope == a->package_scope &&
                !strcmp(symbol->name, import->alias)) {
                import_error(a, import, "Package import alias conflicts with a package declaration");
                break;
            }
        }
    }
}
static const script_package_import *namespace(
    const analyzer *a,
    TSNode node,
    const char *name,
    const char **member
)
{
    if (!a->document_count) {
        return NULL;
    }
    const char *dot = strchr(name, '.');
    if (!dot || dot == name) {
        return NULL;
    }
    size_t length = (size_t)(dot - name);
    int document = script_analyzer_range(a, node).document;
    for (int i = 0; i < a->import_count; i++) {
        const script_package_import *import = &a->imports[i];
        if (import->document != document || !import->alias ||
            strlen(import->alias) != length ||
            strncmp(import->alias, name, length) || !strcmp(import->alias, "_")) {
            continue;
        }
        int local = script_analyzer_lookup_name(a, import->alias);
        if (local < 0) {
            local = script_analyzer_callable_symbol(a, import->alias);
        }
        if (local >= 0 && a->out->symbols[local].scope > a->package_scope) {
            return NULL;
        }
        *member = dot + 1;
        return import;
    }
    return NULL;
}
bool script_analyzer_import_namespace(analyzer *a, TSNode node, const char *name)
{
    const char *member;
    return namespace(a, node, name, &member) != NULL;
}
static const script_package_export *export_member(
    const script_package_import *import,
    const char *name,
    script_package_export_kind kind
)
{
    if (!import->exports) {
        return NULL;
    }
    for (int i = 0; i < import->exports->member_count; i++) {
        const script_package_export *member = &import->exports->members[i];
        if (member->kind == kind && !strcmp(member->name, name)) {
            return member;
        }
    }
    return NULL;
}
bool script_package_private(const script_package_exports *exports, const char *member)
{
    for (int i = 0; exports && i < exports->private_count; i++) {
        if (!strcmp(exports->private_names[i], member)) {
            return true;
        }
    }
    return false;
}
bool script_analyzer_private_member(const analyzer *a, TSNode node, const char *name)
{
    const char *member;
    const script_package_import *import = namespace(a, node, name, &member);
    return import && script_package_private(import->exports, member);
}
static int own_symbol(const analyzer *a, const char *name, script_symbol_kind kind)
{
    for (int i = 0; i < acount(a->out->symbols); i++) {
        const script_symbol *symbol = &a->out->symbols[i];
        if (symbol->scope == a->package_scope && symbol->kind == kind &&
            !strcmp(symbol->name, name)) {
            return i;
        }
    }
    return -1;
}
int script_analyzer_package_function(analyzer *a, TSNode node, const char *name)
{
    const char *member;
    const script_package_import *import = namespace(a, node, name, &member);
    return import && !strcmp(import->identity, a->out->identity)
             ? own_symbol(a, member, SCRIPT_FUNCTION)
             : -1;
}
int script_analyzer_find_type(analyzer *a, TSNode node, const char *name)
{
    script_named_facts facts = script_analyzer_named_facts(a);
    return script_named_find_type(
        &facts,
        a->scope,
        script_analyzer_range(a, node).document,
        name
    );
}

int script_analyzer_find_capability(analyzer *a, TSNode node, const char *name, script_capability_kind kind)
{
    const char *member;
    const script_package_import *import = namespace(a, node, name, &member);
    if (import) {
        const script_package_export *export = export_member(import, member, SCRIPT_PACKAGE_FUNCTION);
        return export && kind == SCRIPT_CAPABILITY_FUNCTION
                 ? capability_index(a, export->id)
                 : -1;
    }
    const script_catalog *visible = a->native ? a->native : a->catalog;
    /* Host package kernels keep their canonical identity in the catalog. Only
     * their owning source package sees the private, exact member spelling.
     * Callers reach the authored public wrapper through ordinary imports. */
    if (a->native && a->out->identity && !strncmp(a->out->identity, "host:", 5)) {
        size_t length = strlen(a->out->identity);
        for (int i = 0; i < visible->capability_count; i++) {
            const script_catalog_capability *capability = &visible->capabilities[i];
            if (capability->kind == kind &&
                !strncmp(capability->name, a->out->identity, length) &&
                capability->name[length] == '.' &&
                !strcmp(capability->name + length + 1, name)) {
                return capability_index(a, capability->id);
            }
        }
    }
    int found = script_catalog_find_capability(visible, name, kind);
    return found >= 0 ? capability_index(a, visible->capabilities[found].id) : found;
}
bool script_analyzer_native_capability(const analyzer *a, uint64_t id)
{
    return !a->native || script_catalog_capability_by_id(a->native, id);
}
int script_analyzer_package_constant(analyzer *a, TSNode node)
{
    char *name = script_analyzer_qualified_name(a, node);
    const char *member;
    const script_package_import *import = namespace(a, node, name, &member);
    if (!import) {
        free(name);
        return -1;
    }
    if (!strcmp(import->identity, a->out->identity)) {
        int symbol = own_symbol(a, member, SCRIPT_CONSTANT);
        free(name);
        if (symbol >= 0) {
            script_analyzer_resolve_initializer(a, symbol);
        }
        return symbol;
    }
    const script_package_export *export = export_member(import, member, SCRIPT_PACKAGE_CONSTANT);
    free(name);
    if (!export) {
        return -1;
    }
    for (int i = 0; i < acount(a->out->symbols); i++) {
        if (a->out->symbols[i].scope == -1 && a->out->symbols[i].kind == SCRIPT_CONSTANT &&
            a->out->symbols[i].id == export->id) {
            return i;
        }
    }
    pin_value value;
    if (pin_value_snapshot_to_arena(&a->out->constants, &value, &export->value)) {
        script_analyzer_diagnostic(a, node, "Cannot retain imported constant");
        return -1;
    }
    int initializer = script_analyzer_expr_add(
        a,
        (script_expression){
            .kind = SCRIPT_EXPR_CONSTANT,
            .type = export->type,
            .constant = value,
            .constant_snapshot = true,
            .range = script_analyzer_range(a, node),
            .symbol = -1,
            .left = -1,
            .right = -1,
            .entry = -1,
        }
    );
    apush(
        a->out->symbols,
        ((script_symbol){
            .id = export->id,
            .name = xstrdup(export->name),
            .kind = SCRIPT_CONSTANT,
            .unit = export->unit ? xstrdup(export->unit) : NULL,
            .type = export->type,
            .scope = -1,
            .initializer = initializer,
            .function = -1,
            .declaration = script_analyzer_range(a, node),
        })
    );
    return acount(a->out->symbols) - 1;
}
