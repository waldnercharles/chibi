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
static bool valid_environment(const script_package_exports *exports)
{
    const script_package_environment *environment = exports->environment;
    if (!environment) {
        return true;
    }
    if (!environment->identity || strcmp(environment->identity, exports->identity) ||
        environment->document_count < 1 ||
        environment->document_count > SCRIPT_PACKAGE_MAX_DOCUMENTS ||
        !environment->documents || environment->import_count < 0 ||
        environment->import_count > 4096 ||
        (environment->import_count && !environment->imports) ||
        environment->binding_count < 0 || environment->binding_count > 4096 ||
        (environment->binding_count && !environment->bindings)) {
        return false;
    }
    for (int i = 0; i < environment->document_count; i++) {
        const script_package_document *document = &environment->documents[i];
        if (!document->path || !document->path[0] || !document->key ||
            !document->key[0] || !document->syntax || !document->syntax->tree ||
            !document->syntax->text) {
            return false;
        }
    }
    for (int i = 0; i < environment->import_count; i++) {
        const script_package_import *import = &environment->imports[i];
        if (import->document < 0 || import->document >= environment->document_count ||
            !identifier(import->alias) || !import->identity || !import->identity[0] ||
            (strcmp(import->identity, environment->identity) &&
             strcmp(import->alias, "_") && !import->exports) ||
            (import->exports &&
             (!import->exports->identity || !import->exports->metadata ||
              strcmp(import->identity, import->exports->identity)))) {
            return false;
        }
    }
    for (int i = 0; i < environment->binding_count; i++) {
        const script_package_export *binding = &environment->bindings[i];
        if (!identifier(binding->name) || !binding->id ||
            binding->kind < SCRIPT_PACKAGE_FUNCTION ||
            binding->kind > SCRIPT_PACKAGE_TEMPLATE) {
            return false;
        }
    }
    return true;
}
static bool valid_table(int count, const void *rows)
{
    return count >= 0 && (!count || rows);
}
static bool valid_export_shape(const script_package_exports *exports)
{
    if (!exports->identity || !exports->metadata || exports->member_count < 0 ||
        exports->member_count > 4096 ||
        (exports->member_count && !exports->members) || exports->private_count < 0 ||
        (exports->private_count && !exports->private_names)) {
        return false;
    }
    const script_catalog *metadata = exports->metadata;
    if (!valid_table(metadata->type_count, metadata->types) ||
        !valid_table(metadata->global_count, metadata->globals) ||
        !valid_table(metadata->relationship_count, metadata->relationships) ||
        !valid_table(metadata->capability_count, metadata->capabilities) ||
        metadata->template_count < 0 || metadata->template_count > 4096 ||
        (metadata->template_count && !metadata->templates) ||
        metadata->specialization_count < 0 || metadata->specialization_count > 4096 ||
        (metadata->specialization_count && !metadata->specializations)) {
        return false;
    }
    for (int i = 0; i < metadata->type_count; i++) {
        const script_catalog_type *type = &metadata->types[i];
        if (!valid_table(type->field_count, type->fields) ||
            !valid_table(type->variant_count, type->variants)) {
            return false;
        }
        for (int j = 0; j < type->variant_count; j++) {
            if (!valid_table(type->variants[j].field_count, type->variants[j].fields)) {
                return false;
            }
        }
    }
    for (int i = 0; i < metadata->capability_count; i++) {
        const script_catalog_capability *capability = &metadata->capabilities[i];
        if (!valid_table(capability->parameter_count, capability->parameters) ||
            !valid_table(capability->result_count, capability->results) ||
            !valid_table(capability->entry_count, capability->entries) ||
            !valid_table(capability->outcome_count, capability->outcomes)) {
            return false;
        }
    }
    if (!valid_environment(exports)) {
        return false;
    }
    for (int i = 0; i < metadata->template_count; i++) {
        const script_catalog_template *t = &metadata->templates[i];
        if (!t->id || !t->name || !t->owner || t->parameter_count < 0 ||
            t->parameter_count > 4096 || (t->parameter_count && !t->parameters)) {
            return false;
        }
        for (int j = 0; j < t->parameter_count; j++) {
            if (!t->parameters[j].name) {
                return false;
            }
        }
    }
    for (int i = 0; i < metadata->specialization_count; i++) {
        const script_catalog_specialization *s = &metadata->specializations[i];
        if (!s->id || !s->template_id || s->binding_count < 0 ||
            s->binding_count > 4096 || (s->binding_count && !s->bindings)) {
            return false;
        }
        for (int j = 0; j < s->binding_count; j++) {
            if (!s->bindings[j].name) {
                return false;
            }
        }
    }
    for (int i = 0; i < exports->member_count; i++) {
        const script_package_export *member = &exports->members[i];
        if (!identifier(member->name) || !member->id ||
            member->kind < SCRIPT_PACKAGE_FUNCTION ||
            member->kind > SCRIPT_PACKAGE_TEMPLATE) {
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
        if (member->kind == SCRIPT_PACKAGE_TEMPLATE) {
            bool found = false;
            for (int j = 0; j < metadata->template_count; j++) {
                const script_catalog_template *t = &metadata->templates[j];
                found |= t->id == member->id && t->owner && t->generic &&
                         t->kind == SCRIPT_TEMPLATE_FUNCTION &&
                         !strcmp(t->owner, exports->identity);
            }
            if (!found) {
                return false;
            }
        }
    }
    return true;
}
static bool valid_exports(const script_package_exports *exports)
{
    const script_package_exports **pending = NULL;
    apush(pending, exports);
    bool valid = true;
    for (int i = 0; valid && i < acount(pending); i++) {
        const script_package_exports *item = pending[i];
        if (!valid_export_shape(item)) {
            valid = false;
            break;
        }
        const script_package_environment *environment = item->environment;
        for (int j = 0; environment && j < environment->import_count; j++) {
            const script_package_exports *dependency = environment->imports[j].exports;
            if (!dependency) {
                continue;
            }
            bool seen = false;
            for (int k = 0; k < acount(pending); k++) {
                seen |= pending[k] == dependency;
            }
            if (!seen) {
                if (acount(pending) == 4096) {
                    valid = false;
                    break;
                }
                apush(pending, dependency);
            }
        }
    }
    afree(pending);
    return valid;
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
        int old_count = a->catalog_view.specialization_count;
        script_catalog_specialization *specializations = arena_alloc(
            &a->out->constants,
            (size_t)(old_count + metadata->specialization_count) * sizeof *specializations
        );
        if (old_count) {
            memcpy(specializations, a->catalog_view.specializations, (size_t)old_count * sizeof *specializations);
        }
        int specialization_count = old_count;
        for (int j = 0; j < metadata->specialization_count; j++) {
            bool found = false;
            for (int k = 0; k < specialization_count; k++) {
                found |= specializations[k].id == metadata->specializations[j].id;
            }
            if (!found) {
                specializations[specialization_count++] = metadata->specializations[j];
            }
        }
        a->catalog_view.specializations = specializations;
        a->catalog_view.specialization_count = specialization_count;
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
        const imported_environment *environment = script_analyzer_environment(a);
        int package_scope = environment ? environment->scope : a->package_scope;
        if (local >= 0 && a->out->symbols[local].scope > package_scope) {
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
const script_catalog_template *script_analyzer_package_template(analyzer *a, TSNode node, const char *name)
{
    const char *member;
    const script_package_import *import = namespace(a, node, name, &member);
    if (!import) {
        return NULL;
    }
    const script_package_export *export = export_member(import, member, SCRIPT_PACKAGE_TEMPLATE);
    if (!export) {
        return NULL;
    }
    const script_catalog *metadata = import->exports->metadata;
    for (int i = 0; i < metadata->template_count; i++) {
        if (metadata->templates[i].id == export->id) {
            return &metadata->templates[i];
        }
    }
    return NULL;
}
static TSNode environment_declaration(analyzer *a, int first, int count, const char *name)
{
    for (int i = 0; i < count; i++) {
        TSNode root = ts_tree_root_node(a->documents[first + i].syntax->tree);
        for (uint32_t j = 0; j < script_analyzer_named_count(root); j++) {
            TSNode node = ts_node_named_child(root, j);
            TSNode token = script_analyzer_field(node, "name");
            if (!ts_node_is_null(token) && script_analyzer_text_is(a, token, name)) {
                return node;
            }
        }
    }
    return (TSNode){ 0 };
}
static bool environment_metadata(analyzer *a, const script_catalog *source)
{
    script_catalog retained;
    if (!script_catalog_snapshot(&a->out->constants, source, &retained)) {
        return false;
    }
    for (int i = 0; i < retained.type_count; i++) {
        if (type_index(a, retained.types[i].id) < 0) {
            apush(a->catalog_types, retained.types[i]);
            a->catalog_view.types = a->catalog_types;
            a->catalog_view.type_count = acount(a->catalog_types);
        }
    }
    for (int i = 0; i < retained.capability_count; i++) {
        if (capability_index(a, retained.capabilities[i].id) < 0) {
            apush(a->catalog_capabilities, retained.capabilities[i]);
            a->catalog_view.capabilities = a->catalog_capabilities;
            a->catalog_view.capability_count = acount(a->catalog_capabilities);
        }
    }
    return true;
}
/* Imported callable templates instantiate in the calling module, but bind
 * every name in a separate definition-site scope. This avoids a reverse code
 * dependency from the library module to the caller's comparator/predicate. */
static int import_environment(analyzer *a, const script_package_exports *exports)
{
    const script_package_environment *environment = exports->environment;
    for (int i = 0; i < acount(a->environments); i++) {
        if (a->environments[i].environment == environment) {
            return a->environments[i].scope;
        }
    }
    if (environment->document_count < 1 ||
        a->document_count > SCRIPT_PACKAGE_MAX_DOCUMENTS - environment->document_count) {
        return -1;
    }
    pin_value *constants = arena_alloc(
        &a->out->constants,
        (size_t)environment->binding_count * sizeof *constants
    );
    for (int i = 0; i < environment->binding_count; i++) {
        const script_package_export *binding = &environment->bindings[i];
        if (binding->kind == SCRIPT_PACKAGE_CONSTANT && pin_value_capture_to_arena(
                                                            &a->out->constants,
                                                            &constants[i],
                                                            &binding->value
                                                        )) {
            return -1;
        }
    }
    if (!environment_metadata(a, exports->metadata)) {
        return -1;
    }
    for (int i = 0; i < environment->import_count; i++) {
        const script_package_exports *dependency = environment->imports[i].exports;
        if (dependency && !environment_metadata(a, dependency->metadata)) {
            return -1;
        }
    }
    if (!a->imported_documents) {
        for (int i = 0; i < a->document_count; i++) {
            apush(a->imported_documents, a->documents[i]);
        }
    }
    int first = a->document_count;
    a->out->documents = xrealloc(
        a->out->documents,
        (size_t)(first + environment->document_count) * sizeof *a->out->documents
    );
    for (int i = 0; i < environment->document_count; i++) {
        const script_package_document *document = &environment->documents[i];
        apush(a->imported_documents, *document);
        a->out->documents[first + i] = (script_source_document){
            xstrdup(document->path),
            xstrdup(document->key),
            xstrdup(document->syntax->text)
        };
    }
    a->documents = a->imported_documents;
    a->document_count = acount(a->imported_documents);
    a->out->document_count = a->document_count;
    if (!a->imported_imports) {
        for (int i = 0; i < a->import_count; i++) {
            apush(a->imported_imports, a->imports[i]);
        }
    }
    for (int i = 0; i < environment->import_count; i++) {
        script_package_import import = environment->imports[i];
        import.document += first;
        import.range.document += first;
        apush(a->imported_imports, import);
    }
    a->imports = a->imported_imports;
    int old_import_count = a->import_count;
    a->import_count = acount(a->imported_imports);
    a->import_used = xrealloc(
        a->import_used,
        (size_t)a->import_count * sizeof *a->import_used
    );
    for (int i = old_import_count; i < a->import_count; i++) {
        a->import_used[i] = true;
    }
    int saved_scope = a->scope, saved_function = a->function;
    /* Scope zero contains native declarations. The caller's package scope is
     * deliberately not an ancestor of this definition environment. */
    a->scope = 0;
    a->function = -1;
    int scope = script_analyzer_scope_new(
        a,
        ts_tree_root_node(environment->documents[0].syntax->tree)
    );
    a->scope = scope;
    apush(a->environments, ((imported_environment){ environment, scope, first }));
    for (int i = 0; i < environment->binding_count; i++) {
        const script_package_export *binding = &environment->bindings[i];
        TSNode declaration = environment_declaration(
            a,
            first,
            environment->document_count,
            binding->name
        );
        if (binding->kind == SCRIPT_PACKAGE_TEMPLATE) {
            if (!ts_node_is_null(declaration) &&
                script_analyzer_generic_declare(a, declaration)) {
                int symbol = script_analyzer_callable_symbol(a, binding->name);
                uint64_t previous_id = a->out->symbols[symbol].id;
                a->out->symbols[symbol].id = binding->id;
                for (int j = 0; j < acount(a->out->templates); j++) {
                    if (a->out->templates[j].id == previous_id) {
                        a->out->templates[j].id = binding->id;
                    }
                }
            }
            continue;
        }
        script_symbol symbol = { .id = binding->id,
                                 .name = xstrdup(binding->name),
                                 .type = binding->type,
                                 .scope = scope,
                                 .initializer = -1,
                                 .function = -1,
                                 .declaration = script_analyzer_range(a, declaration),
                                 .unit = binding->unit ? xstrdup(binding->unit) : NULL };
        if (binding->kind == SCRIPT_PACKAGE_FUNCTION) {
            symbol.kind = SCRIPT_FUNCTION;
            symbol.capability = binding->id;
            symbol.callable_binding = true;
        } else if (binding->kind == SCRIPT_PACKAGE_TYPE) {
            symbol.kind = SCRIPT_TYPE;
        } else {
            symbol.kind = SCRIPT_CONSTANT;
            symbol.initializer = script_analyzer_expr_add(
                a,
                (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                     .type = binding->type,
                                     .constant = constants[i],
                                     .constant_snapshot = true,
                                     .range = symbol.declaration,
                                     .symbol = -1,
                                     .left = -1,
                                     .right = -1,
                                     .entry = -1 }
            );
        }
        apush(a->out->symbols, symbol);
    }
    a->scope = saved_scope;
    a->function = saved_function;
    return scope;
}
int script_analyzer_package_specialize(analyzer *a, TSNode call, const char *name, int **arguments)
{
    const char *member;
    const script_package_import *import = namespace(a, call, name, &member);
    const script_package_exports *exports = import ? import->exports : NULL;
    const script_package_export *export = import ? export_member(import, member, SCRIPT_PACKAGE_TEMPLATE)
                                                 : NULL;
    if (!export || !exports->environment) {
        return -1;
    }
    bool callable = false;
    const script_catalog_template *definition = NULL;
    const script_catalog *metadata = exports->metadata;
    for (int i = 0; i < metadata->template_count; i++) {
        const script_catalog_template *t = &metadata->templates[i];
        if (t->id == export->id) {
            definition = t;
            for (int j = 0; j < t->parameter_count; j++) {
                callable |= t->parameters[j].callable_type != NULL;
            }
        }
    }
    if (!callable) {
        TSNode values = script_analyzer_field(call, "arguments");
        for (uint32_t i = 0; i < script_analyzer_named_count(values); i++) {
            TSNode argument = ts_node_named_child(values, i);
            if (ts_node_is_extra(argument)) {
                continue;
            }
            int parameter = (int)i;
            TSNode label = script_analyzer_field(argument, "name");
            if (!ts_node_is_null(label)) {
                parameter = -1;
                for (int j = 0; definition && j < definition->parameter_count; j++) {
                    if (script_analyzer_text_is(
                            a,
                            label,
                            definition->parameters[j].name
                        )) {
                        parameter = j;
                        break;
                    }
                }
            }
            if (!definition || parameter < 0 || parameter >= definition->parameter_count ||
                definition->parameters[parameter].comptime ||
                definition->parameters[parameter].type_value ||
                !definition->parameters[parameter].binder) {
                continue;
            }
            TSNode value = script_analyzer_call_argument(argument);
            int expression = script_analyzer_expression(a, value, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
            script_type type = script_analyzer_expr_type(a, expression);
            for (int depth = 0;
                 depth < 16 && (type.container == PIN_CONTAINER_ARRAY ||
                                type.container == PIN_CONTAINER_VIEW);
                 depth++) {
                type = script_catalog_element_type(a->catalog, type);
            }
            callable |= type.nominal &&
                        !script_catalog_type_by_id(metadata, type.nominal);
        }
        if (!callable) {
            return -1;
        }
    }
    int scope = import_environment(a, exports);
    if (scope < 0) {
        script_analyzer_diagnostic(a, call, "Cannot retain generic definition environment");
        return -1;
    }
    int symbol = -1;
    for (int i = 0; i < acount(a->out->symbols); i++) {
        if (a->out->symbols[i].scope == scope && a->out->symbols[i].id == export->id) {
            symbol = i;
            break;
        }
    }
    int before = acount(a->out->diagnostics);
    int function = symbol >= 0
                     ? script_analyzer_generic_call(a, call, symbol, arguments)
                     : -1;
    for (int i = before; i < acount(a->out->diagnostics); i++) {
        script_diagnostic *diagnostic = &a->out->diagnostics[i];
        if (diagnostic->range.document < a->out->package_document_count) {
            continue;
        }
        const script_source_document
            *source = &a->out->documents[diagnostic->range.document];
        char message[sizeof diagnostic->message];
        snprintf(
            message,
            sizeof message,
            "%.64s:%d:%d: %.96s",
            source->path,
            diagnostic->range.line,
            diagnostic->range.column,
            diagnostic->message
        );
        snprintf(diagnostic->message, sizeof diagnostic->message, "%s", message);
        diagnostic->range = script_analyzer_range(a, call);
    }
    return function;
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
const imported_environment *script_analyzer_environment(const analyzer *a)
{
    for (int scope = a->scope; scope >= 0; scope = a->out->scopes[scope].parent) {
        for (int i = 0; i < acount(a->environments); i++) {
            if (a->environments[i].scope == scope) {
                return &a->environments[i];
            }
        }
    }
    return NULL;
}
static int own_symbol(const analyzer *a, const char *name, script_symbol_kind kind)
{
    const imported_environment *environment = script_analyzer_environment(a);
    int scope = environment ? environment->scope : a->package_scope;
    for (int i = 0; i < acount(a->out->symbols); i++) {
        const script_symbol *symbol = &a->out->symbols[i];
        if (symbol->scope == scope && symbol->kind == kind &&
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
    const imported_environment *environment = script_analyzer_environment(a);
    const char *identity = environment ? environment->environment->identity
                                       : a->out->identity;
    return import && !strcmp(import->identity, identity)
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
    const imported_environment *environment = script_analyzer_environment(a);
    const char *identity = environment ? environment->environment->identity
                                       : a->out->identity;
    if (!strcmp(import->identity, identity)) {
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
    if (pin_value_capture_to_arena(&a->out->constants, &value, &export->value)) {
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
