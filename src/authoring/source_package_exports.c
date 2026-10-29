#include "authoring/source_package_exports.h"

#include "capabilities/catalog_snapshot.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct source_package_exports
{
    arena storage;
    script_catalog metadata;
    script_package_exports view;
    script_package_environment environment;
    script_syntax **syntaxes;
    source_package_exports **dependencies;
    int references;
    char error[192];
};
typedef struct
{
    source_package_exports *owner;
    const script_analysis *analysis;
    const script_catalog *metadata;
    const script_lower_package_result *lowered;
    script_package_export *members;
    script_package_export *bindings;
    bool environment;
    const char **private_names;
    script_catalog_capability *functions, *capabilities;
    script_catalog_type *types;
    script_catalog_template *templates;
} exports_builder;

static const char *own(source_package_exports *owner, const char *text)
{
    if (!text) {
        return NULL;
    }
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(&owner->storage, size);
    memcpy(copy, text, size);
    return copy;
}
static bool reject(exports_builder *b, const char *message)
{
    snprintf(b->owner->error, sizeof b->owner->error, "%s", message);
    return false;
}
static const script_catalog_type *type_by_id(const exports_builder *b, uint64_t id)
{
    for (int i = 0; i < b->analysis->type_count; i++) {
        if (b->analysis->types[i].id == id) {
            return &b->analysis->types[i];
        }
    }
    return script_catalog_type_by_id(b->metadata, id);
}
static const script_catalog_capability *capability_by_id(const exports_builder *b, uint64_t id)
{
    for (int i = 0; i < acount(b->functions); i++) {
        if (b->functions[i].id == id) {
            return &b->functions[i];
        }
    }
    return script_catalog_capability_by_id(b->metadata, id);
}
static bool include_capability(exports_builder *b, uint64_t id)
{
    if (!id) {
        return true;
    }
    for (int i = 0; i < acount(b->capabilities); i++) {
        if (b->capabilities[i].id == id) {
            return true;
        }
    }
    const script_catalog_capability *capability = capability_by_id(b, id);
    if (!capability) {
        return reject(b, "Package export references missing callable metadata");
    }
    apush(b->capabilities, *capability);
    return true;
}
static bool include_type(exports_builder *b, uint64_t id)
{
    if (!id) {
        return true;
    }
    for (int i = 0; i < acount(b->types); i++) {
        if (b->types[i].id == id) {
            return true;
        }
    }
    const script_catalog_type *type = type_by_id(b, id);
    if (!type) {
        return reject(b, "Package export references missing nominal type metadata");
    }
    apush(b->types, *type);
    return true;
}
static bool include_signature(exports_builder *b, script_type type)
{
    if (type.collection && !include_type(b, type.collection)) {
        return false;
    }
    if (!type.nominal) {
        return true;
    }
    if (type_by_id(b, type.nominal)) {
        return include_type(b, type.nominal);
    }
    if (capability_by_id(b, type.nominal)) {
        return include_capability(b, type.nominal);
    }
    if ((type.storage == PIN_STRUCT || type.storage == PIN_UNION ||
         type.storage == PIN_POINTER) &&
        !(type.flags & SCRIPT_TYPE_GENERIC)) {
        return reject(b, "Package export references missing nominal type metadata");
    }
    return true;
}
static const pin_value *constant_value(const exports_builder *b, uint64_t id)
{
    for (int i = 0; b->lowered && i < b->lowered->constant_count; i++) {
        if (b->lowered->constants[i].id == id) {
            return &b->lowered->constants[i].value;
        }
    }
    if (!b->lowered) {
        for (int i = 0; i < b->analysis->symbol_count; i++) {
            const script_symbol *symbol = &b->analysis->symbols[i];
            if (symbol->id == id && symbol->initializer >= 0) {
                const script_expression *e = &b->analysis->expressions[symbol->initializer];
                if (e->kind == SCRIPT_EXPR_CONSTANT) {
                    return &e->constant;
                }
            }
        }
    }
    return NULL;
}
static bool parameters(exports_builder *b, const int *symbols, int count, const script_catalog_parameter **out)
{
    script_catalog_parameter *values = arena_alloc(&b->owner->storage, (size_t)count * sizeof *values);
    for (int i = 0; i < count; i++) {
        const script_symbol *symbol = &b->analysis->symbols[symbols[i]];
        values[i] = (script_catalog_parameter){ .name = symbol->name,
                                                .unit = symbol->unit,
                                                .type = symbol->type };
        if (symbol->initializer >= 0) {
            const pin_value *value = constant_value(b, symbol->id);
            if (!value) {
                if (!b->lowered) {
                    continue;
                }
                return reject(b, "Package parameter default has no lowered constant value");
            }
            values[i].default_value = *value;
            values[i].has_default = true;
        }
    }
    *out = values;
    return true;
}
static bool function(exports_builder *b, const script_symbol *symbol)
{
    const script_function *f = &b->analysis->functions[symbol->function];
    static const char *const entries[] = { "entry" }, *const outcomes[] = {
        "Then"
    };
    size_t size = strlen(b->analysis->identity) + strlen(symbol->name) + 2;
    char *name = arena_alloc(&b->owner->storage, size);
    snprintf(name, size, "%s.%s", b->analysis->identity, symbol->name);
    script_catalog_capability capability = { .id = symbol->id,
                                             .name = name,
                                             .qualified_name = name,
                                             .kind = SCRIPT_CAPABILITY_FUNCTION,
                                             .parameter_count = f->parameter_count,
                                             .result_count = f->result_count,
                                             .entries = entries,
                                             .entry_count = 1,
                                             .outcomes = outcomes,
                                             .outcome_count = 1,
                                             .completion_outcome = 0,
                                             .effects = f->effects,
                                             .pure = f->pure,
                                             .fallible = f->fallible };
    if (!parameters(b, f->parameters, f->parameter_count, &capability.parameters) ||
        !parameters(b, f->results, f->result_count, &capability.results)) {
        return false;
    }
    apush(b->functions, capability);
    return true;
}
static bool members(exports_builder *b)
{
    const script_analysis *a = b->analysis;
    for (int i = 0; i < a->symbol_count; i++) {
        const script_symbol *symbol = &a->symbols[i];
        if (symbol->scope != a->package_scope ||
            symbol->declaration.start < 0 || symbol->disabled) {
            continue;
        }
        if (!symbol->pub) {
            apush(b->private_names, symbol->name);
            if (!b->environment) {
                continue;
            }
        }
        script_package_export member = { .name = symbol->name,
                                         .id = symbol->id,
                                         .type = symbol->type,
                                         .unit = symbol->unit };
        switch (symbol->kind) {
            case SCRIPT_FUNCTION:
                if (symbol->function < 0) {
                    const script_catalog_template *t = NULL;
                    for (int j = 0; j < a->template_count; j++) {
                        if (a->templates[j].id == symbol->id && a->templates[j].generic &&
                            a->templates[j].kind == SCRIPT_TEMPLATE_FUNCTION) {
                            t = &a->templates[j];
                        }
                    }
                    if (!t) {
                        continue;
                    }
                    member.kind = SCRIPT_PACKAGE_TEMPLATE;
                    apush(b->templates, *t);
                    break;
                }
                member.kind = SCRIPT_PACKAGE_FUNCTION;
                if (!function(b, symbol)) {
                    return false;
                }
                break;
            case SCRIPT_TYPE: member.kind = SCRIPT_PACKAGE_TYPE; break;
            case SCRIPT_CONSTANT: {
                member.kind = SCRIPT_PACKAGE_CONSTANT;
                const pin_value *value = constant_value(b, symbol->id);
                if (!value) {
                    if (!b->lowered) {
                        continue;
                    }
                    return reject(b, "Package export has no lowered constant value");
                }
                member.value = *value;
                break;
            }
            default: continue;
        }
        if (symbol->pub) {
            apush(b->members, member);
        }
        if (b->environment) {
            apush(b->bindings, member);
        }
    }
    return true;
}
static bool fields(exports_builder *b, const script_catalog_field *fields, int count)
{
    for (int i = 0; i < count; i++) {
        if (!include_signature(b, fields[i].type) ||
            !include_capability(b, fields[i].read) ||
            !include_capability(b, fields[i].write)) {
            return false;
        }
    }
    return true;
}
static bool closure(exports_builder *b)
{
    for (int i = 0; i < acount(b->members); i++) {
        const script_package_export *member = &b->members[i];
        if (member->kind == SCRIPT_PACKAGE_TEMPLATE) {
            continue;
        }
        if (!(member->kind == SCRIPT_PACKAGE_FUNCTION
                  ? include_capability(b, member->id)
                  : include_signature(b, member->type))) {
            return false;
        }
    }
    for (int i = 0; i < acount(b->bindings); i++) {
        const script_package_export *member = &b->bindings[i];
        if (member->kind == SCRIPT_PACKAGE_TEMPLATE) {
            continue;
        }
        if (!(member->kind == SCRIPT_PACKAGE_FUNCTION
                  ? include_capability(b, member->id)
                  : include_signature(b, member->type))) {
            return false;
        }
    }
    int type_index = 0, capability_index = 0;
    while (type_index < acount(b->types) || capability_index < acount(b->capabilities)) {
        while (type_index < acount(b->types)) {
            /* Appending the worklist can move it; retain a shallow value. */
            script_catalog_type type = b->types[type_index++];
            if (!include_signature(b, type.type) ||
                !include_signature(b, type.element_type) ||
                !include_signature(b, type.key_type) ||
                !fields(b, type.fields, type.field_count)) {
                return false;
            }
            for (int i = 0; i < type.variant_count; i++) {
                if (!include_signature(b, type.variants[i].type) ||
                    !fields(b, type.variants[i].fields, type.variants[i].field_count)) {
                    return false;
                }
            }
        }
        while (capability_index < acount(b->capabilities)) {
            script_catalog_capability capability = b->capabilities[capability_index++];
            const uint64_t ids[] = { capability.component,
                                     capability.aggregate,
                                     capability.receiver };
            for (int i = 0; i < 3; i++) {
                if (ids[i] && type_by_id(b, ids[i]) && !include_type(b, ids[i])) {
                    return false;
                }
            }
            for (int result = 0; result < 2; result++) {
                const script_catalog_parameter *p = result ? capability.results
                                                           : capability.parameters;
                int count = result ? capability.result_count : capability.parameter_count;
                for (int i = 0; i < count; i++) {
                    if (!include_signature(b, p[i].type)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}
source_package_exports *source_package_exports_new(
    const script_analysis *analysis,
    const script_catalog *metadata,
    const script_lower_package_result *lowered
)
{
    assert(analysis && metadata);
    source_package_exports *owner = xcalloc(1, sizeof *owner);
    owner->references = 1;
    exports_builder b = { .owner = owner,
                          .analysis = analysis,
                          .metadata = metadata,
                          .lowered = lowered };
    for (int i = 0; i < analysis->template_count; i++) {
        bool exported = false;
        for (int j = 0; j < analysis->symbol_count; j++) {
            const script_symbol *symbol = &analysis->symbols[j];
            exported |= symbol->id == analysis->templates[i].id &&
                        symbol->pub && symbol->scope == analysis->package_scope;
        }
        if (!exported) {
            continue;
        }
        b.environment |= analysis->templates[i].generic &&
                         analysis->templates[i].kind == SCRIPT_TEMPLATE_FUNCTION;
        for (int j = 0; j < analysis->templates[i].parameter_count; j++) {
            b.environment |= analysis->templates[i].parameters[j].callable_type != NULL;
        }
    }
    if (analysis->package_scope < 0 ||
        (lowered && (!analysis->valid || !lowered->plan || !lowered->plan->valid))) {
        reject(&b, "Package exports require a valid lowered package");
        return owner;
    }
    bool valid = members(&b);
    for (int i = 0; valid && i < analysis->specialization_count; i++) {
        const script_catalog_specialization *s = &analysis->specializations[i];
        if (!s->executable) {
            continue;
        }
        for (int j = 0; j < analysis->function_count; j++) {
            const script_function *f = &analysis->functions[j];
            if (f->symbol >= 0 && analysis->symbols[f->symbol].id == s->id) {
                valid = function(&b, &analysis->symbols[f->symbol]) &&
                        include_capability(&b, s->id);
                break;
            }
        }
    }
    if (valid && closure(&b)) {
        script_catalog selected = { .types = b.types,
                                    .type_count = acount(b.types),
                                    .capabilities = b.capabilities,
                                    .capability_count = acount(b.capabilities),
                                    .templates = b.templates,
                                    .template_count = acount(b.templates),
                                    .specializations = analysis->specializations,
                                    .specialization_count = analysis->specialization_count,
                                    .entity_type = metadata->entity_type,
                                    .tick_event = metadata->tick_event };
        if (!script_catalog_snapshot(&owner->storage, &selected, &owner->metadata)) {
            reject(&b, "Cannot retain package export metadata");
        }
    }
    if (!owner->error[0]) {
        script_package_export *retained = arena_alloc(
            &owner->storage,
            (size_t)acount(b.members) * sizeof *retained
        );
        for (int i = 0; i < acount(b.members); i++) {
            retained[i] = b.members[i];
            retained[i].name = own(owner, retained[i].name);
            retained[i].unit = own(owner, retained[i].unit);
            if (retained[i].kind == SCRIPT_PACKAGE_CONSTANT &&
                pin_value_capture_to_arena(
                    &owner->storage,
                    &retained[i].value,
                    &b.members[i].value
                )) {
                reject(&b, "Cannot retain package constant export");
                break;
            }
        }
        const char **hidden = arena_alloc(&owner->storage, (size_t)acount(b.private_names) * sizeof *hidden);
        for (int i = 0; i < acount(b.private_names); i++) {
            hidden[i] = own(owner, b.private_names[i]);
        }
        if (!owner->error[0]) {
            owner->view = (script_package_exports){
                .identity = own(owner, analysis->identity),
                .metadata = &owner->metadata,
                .members = retained,
                .member_count = acount(b.members),
                .private_names = hidden,
                .private_count = acount(b.private_names),
                .owner = owner
            };
            if (b.environment) {
                int document_count = analysis->package_document_count;
                script_package_document *documents = arena_alloc(
                    &owner->storage,
                    (size_t)document_count * sizeof *documents
                );
                for (int i = 0; i < document_count; i++) {
                    const script_source_document *source = &analysis->documents[i];
                    script_syntax *syntax = script_parse(source->text);
                    apush(owner->syntaxes, syntax);
                    documents[i] = (script_package_document){ own(owner, source->path),
                                                              own(owner, source->key),
                                                              syntax };
                }
                script_package_export *bindings = arena_alloc(
                    &owner->storage,
                    (size_t)acount(b.bindings) * sizeof *bindings
                );
                for (int i = 0; i < acount(b.bindings); i++) {
                    bindings[i] = b.bindings[i];
                    bindings[i].name = own(owner, bindings[i].name);
                    bindings[i].unit = own(owner, bindings[i].unit);
                    if (bindings[i].kind == SCRIPT_PACKAGE_CONSTANT) {
                        int error = pin_value_capture_to_arena(
                            &owner->storage,
                            &bindings[i].value,
                            &b.bindings[i].value
                        );
                        assert(!error);
                        (void)error;
                    }
                }
                owner->environment = (script_package_environment){
                    .identity = owner->view.identity,
                    .documents = documents,
                    .document_count = document_count,
                    .bindings = bindings,
                    .binding_count = acount(b.bindings),
                    .owner = owner
                };
                owner->view.environment = &owner->environment;
            }
        }
    }
    afree(b.members);
    afree(b.private_names);
    afree(b.functions);
    afree(b.types);
    afree(b.capabilities);
    afree(b.templates);
    afree(b.bindings);
    return owner;
}
const script_package_exports *source_package_exports_view(const source_package_exports *owner)
{
    return owner->error[0] ? NULL : &owner->view;
}
const char *source_package_exports_error(const source_package_exports *owner)
{
    return owner->error[0] ? owner->error : NULL;
}
void source_package_exports_free(source_package_exports *owner)
{
    if (owner && !--owner->references) {
        for (int i = 0; i < acount(owner->syntaxes); i++) {
            script_syntax_free(owner->syntaxes[i]);
        }
        for (int i = 0; i < acount(owner->dependencies); i++) {
            source_package_exports_free(owner->dependencies[i]);
        }
        afree(owner->syntaxes);
        afree(owner->dependencies);
        arena_free(&owner->storage);
        free(owner);
    }
}
void source_package_exports_bind_imports(
    source_package_exports *owner,
    const script_package_import *imports,
    int count
)
{
    if (!owner->view.environment || !count) {
        return;
    }
    script_package_import *retained = arena_alloc(&owner->storage, (size_t)count * sizeof *retained);
    for (int i = 0; i < count; i++) {
        retained[i] = imports[i];
        retained[i].alias = own(owner, imports[i].alias);
        retained[i].identity = own(owner, imports[i].identity);
        source_package_exports *dependency = imports[i].exports
                                               ? imports[i].exports->owner
                                               : NULL;
        if (dependency) {
            assert(dependency && dependency != owner && dependency->references > 0);
            dependency->references++;
            apush(owner->dependencies, dependency);
        }
    }
    owner->environment.imports = retained;
    owner->environment.import_count = count;
}
