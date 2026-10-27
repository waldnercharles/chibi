#include "authoring/source_edit_internal.h"
#include "authoring/source_edit_refactor_internal.h"
#include "authoring/source_literal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CREATE_PARAMETERS 256

static bool fail(char *error, size_t size, const char *text)
{
    if (size) {
        snprintf(error, size, "%s", text);
    }
    return false;
}
static void append(char **text, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int bytes = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    int first = acount(*text);
    asetlen(*text, first + bytes + 1);
    vsnprintf(*text + first, (size_t)bytes + 1, format, args);
    asetlen(*text, first + bytes);
    va_end(args);
}
static char *take(char **text)
{
    append(text, "%c", '\0');
    char *out = xstrdup(*text);
    afree(*text);
    return out;
}
static char *quoted(const script_catalog *catalog, const char *name, char *error, size_t size)
{
    (void)catalog;
    return authoring_source_identifier(name, error, size);
}
static char *component_expression(
    const script_catalog *catalog,
    const script_catalog_capability *cap,
    char **inputs,
    int producer,
    int *producer_offset,
    char *error,
    size_t size
)
{
    const script_catalog_type *type = script_catalog_type_by_id(catalog, cap->component);
    if (!type || !type->component) {
        fail(error, size, "Component access requires a declared component type");
        return NULL;
    }
    char *name = authoring_source_identifier(
        type->source_name ? type->source_name : type->name,
        error,
        size
    );
    if (!name) {
        return NULL;
    }
    const char *entity = cap->component_entity >= 0 &&
                                 cap->component_entity < cap->parameter_count
                           ? inputs[cap->component_entity]
                           : NULL;
    char *text = NULL;
    if (!entity || !strcmp(entity, "self")) {
        append(&text, "self.%s", name);
        if (producer == cap->component_entity) {
            *producer_offset = 0;
        }
    } else {
        append(&text, "(%s).%s", entity, name);
        if (producer == cap->component_entity) {
            *producer_offset = 1;
        }
    }
    free(name);
    if (cap->component_field) {
        const script_catalog_field *field = NULL;
        for (int i = 0; i < type->field_count; i++) {
            if (type->fields[i].id == cap->component_field) {
                field = &type->fields[i];
                break;
            }
        }
        name = field ? authoring_source_identifier(field->name, error, size) : NULL;
        if (!name) {
            afree(text);
            fail(error, size, "Component access requires a declared field");
            return NULL;
        }
        append(&text, ".%s", name);
        free(name);
    }
    if (cap->component_access == SCRIPT_COMPONENT_ACCESS_WRITE) {
        const char *value = cap->component_value >= 0 &&
                                    cap->component_value < cap->parameter_count
                              ? inputs[cap->component_value]
                              : NULL;
        if (!value) {
            afree(text);
            fail(error, size, "Supply a component value before creating its assignment");
            return NULL;
        }
        append(&text, " = ");
        if (producer == cap->component_value) {
            *producer_offset = acount(text);
        }
        append(&text, "%s", value);
    } else if (cap->component_access == SCRIPT_COMPONENT_ACCESS_HAS) {
        append(&text, " != nil");
    }
    return take(&text);
}
static bool default_inputs(
    const script_catalog *catalog,
    const script_catalog_capability *cap,
    char **inputs,
    char *error,
    size_t size
)
{
    for (int i = 0; i < cap->parameter_count; i++) {
        if (inputs[i]) {
            continue;
        }
        const script_catalog_parameter *p = &cap->parameters[i];
        if (p->default_owner) {
            inputs[i] = xstrdup("self");
        } else if (p->has_default) {
            inputs[i] = authoring_source_literal(catalog, p->type, &p->default_value, error, size);
        } else if (p->type.flags & SCRIPT_TYPE_OPTIONAL) {
            inputs[i] = xstrdup("nil");
        }
        if (!inputs[i]) {
            return fail(error, size, "Supply the operation's required operands");
        }
    }
    return true;
}
static char *operator_expression(
    const script_catalog_capability *cap,
    char **inputs,
    int producer,
    int *producer_offset,
    char *error,
    size_t size
)
{
    const char *op = cap->operator_name;
    const char *binary[] = { "+",  "-",  "*",   "/",  "%",  "==", "!=",
                             "<",  "<=", ">",   ">=", "&",  "|",  "^",
                             "<<", ">>", "and", "or", "&&", "||" };
    bool valid = cap->parameter_count == 1 &&
                 (!strcmp(op, "!") || !strcmp(op, "not") || !strcmp(op, "-") ||
                  !strcmp(op, "~"));
    for (int i = 0;
         cap->parameter_count == 2 && i < (int)(sizeof binary / sizeof *binary);
         i++) {
        valid |= !strcmp(op, binary[i]);
    }
    if (!valid) {
        fail(error, size, "This capability has no supported operator spelling");
        return NULL;
    }
    char *text = NULL;
    if (cap->parameter_count == 1) {
        append(&text, "%s ", op);
    }
    for (int i = 0; i < cap->parameter_count; i++) {
        if (i) {
            append(&text, " %s ", op);
        }
        append(&text, "(");
        script_type expected = cap->parameters[i].type;
        bool convert = script_type_numeric(expected);
        if (convert) {
            append(&text, "%s(", script_scalar_name(expected.storage));
        }
        if (producer == i) {
            *producer_offset = acount(text);
        }
        append(&text, "%s%s)", inputs[i], convert ? ")" : "");
    }
    return take(&text);
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
static const script_catalog_capability *capability(
    const script_catalog *catalog,
    const authoring_edit_request *r
)
{
    if (r->capability) {
        return script_catalog_capability_by_id(catalog, r->capability);
    }
    const script_catalog_capability *found = NULL;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        const char *name = c->qualified_name ? c->qualified_name : c->name;
        if (r->capability_name && !strcmp(name, r->capability_name)) {
            if (found) {
                return NULL;
            }
            found = c;
        }
    }
    return found;
}
/* A created handler lists every payload field with its type, so each one can
 * be wired at once; the fields it does not read can be deleted. A measured
 * field spells its storage before its unit, as `double charge`, since a lone
 * name reads as a unit only for a few shorthands. */
static bool handler_header(
    const script_catalog *catalog,
    const char *callee,
    const script_catalog_capability *event,
    char **fragment,
    char *error,
    size_t size
)
{
    append(fragment, "on %s :: (", callee);
    for (int i = 0; i < event->parameter_count; i++) {
        const script_catalog_parameter *field = &event->parameters[i];
        char *name = quoted(catalog, field->name, error, size), *type = NULL;
        if (!name) {
            return false;
        }
        if (!source_refactor_type(catalog, field->type, false, &type, error, size)) {
            free(name);
            return false;
        }
        if (field->unit) {
            source_refactor_append(&type, " %s", field->unit);
        }
        char *spelled = source_refactor_take(&type);
        append(fragment, "%s%s: %s", i ? ", " : "", name, spelled);
        free(name);
        free(spelled);
    }
    append(fragment, ") {}\n");
    return true;
}
static void unique_name(const script_analysis *a, char *name, size_t size)
{
    for (int n = 1;; n++) {
        snprintf(name, size, "created_%d", n);
        bool used = false;
        for (int i = 0; i < a->symbol_count; i++) {
            used |= !strcmp(name, a->symbols[i].name);
        }
        if (!used) {
            return;
        }
    }
}
static char *reference(
    const script_catalog *catalog,
    const script_catalog_parameter *p,
    const char *name,
    char *error,
    size_t size
)
{
    if (p->reference_kind == SCRIPT_REFERENCE_RELATIONSHIP) {
        if (script_name_matches(name, "ChildOf")) {
            return xstrdup("ChildOf");
        }
        const script_catalog_relationship *relationship = NULL;
        for (int i = 0; i < catalog->relationship_count; i++) {
            if (!script_name_matches(catalog->relationships[i].name, name)) {
                continue;
            }
            if (relationship) {
                fail(error, size, "Choose one unambiguous relationship reference");
                return NULL;
            }
            relationship = &catalog->relationships[i];
        }
        if (!relationship) {
            fail(error, size, "Choose a registered relationship reference");
            return NULL;
        }
        return quoted(catalog, relationship->name, error, size);
    }
    if (p->reference_kind == SCRIPT_REFERENCE_COMPONENT) {
        int index = script_catalog_find_type(catalog, name);
        if (index < 0 || !catalog->types[index].component) {
            fail(error, size, "Choose a registered component reference");
            return NULL;
        }
        const script_catalog_type *type = &catalog->types[index];
        return quoted(catalog, type->source_name ? type->source_name : type->name, error, size);
    }
    int index = script_catalog_find_capability(catalog, name, SCRIPT_CAPABILITY_PROTOTYPE);
    if (index < 0) {
        fail(error, size, "Choose a registered prototype reference");
        return NULL;
    }
    return quoted(catalog, catalog->capabilities[index].name, error, size);
}
static void variable_read_name(const script_analysis *a, const char *variable, char out[64])
{
    char base[48];
    int length = 0;
    for (const char *p = variable; *p && length < 40; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_' ||
            (length && *p >= '0' && *p <= '9')) {
            base[length++] = *p;
        }
    }
    if (!length) {
        memcpy(base, "read", 4);
        length = 4;
    }
    memcpy(base + length, "Value", 6);
    for (int ordinal = 1;; ordinal++) {
        if (ordinal == 1) {
            snprintf(out, 64, "%s", base);
        } else {
            snprintf(out, 64, "%s%d", base, ordinal);
        }
        bool used = false;
        for (int i = 0; i < a->symbol_count; i++) {
            used |= !strcmp(a->symbols[i].name, out);
        }
        if (!used) {
            return;
        }
    }
}
/* Variable creation uses the declared state identity, never a mutable graph
 * variable or a generated getter/setter capability. */
static bool create_variable_access(
    const script_analysis *a,
    const script_catalog *catalog,
    const authoring_source_presentation *p,
    const authoring_edit_request *r,
    source_edit_changes *change,
    char *error,
    size_t size
)
{
    const script_symbol *state = NULL;
    const source_presentation_origin *declaration = NULL;
    for (int i = 0; i < a->symbol_count; i++) {
        if (a->symbols[i].id == r->symbol &&
            (a->symbols[i].kind == SCRIPT_STATE ||
             a->symbols[i].kind == SCRIPT_SHARED_STATE)) {
            state = &a->symbols[i];
            break;
        }
    }
    if (!state) {
        return fail(error, size, "Choose a declared variable");
    }
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->kind == SOURCE_PRESENTATION_SYMBOL && !o->pin &&
            a->symbols[o->record].id == state->id) {
            declaration = o;
            break;
        }
    }
    if (!declaration) {
        return fail(error, size, "Variable declaration is no longer available");
    }
    bool set = r->kind == AUTHORING_EDIT_CREATE_VARIABLE_SET;
    if ((!set && r->producer) || (set && (!state->writable || r->previous)) ||
        (!r->origin && !r->previous)) {
        return fail(error, size, "Choose a variable access and an executable source position");
    }
    char *name = quoted(catalog, state->name, error, size);
    if (!name) {
        return false;
    }
    char *reference = NULL;
    append(&reference, "state.%s", name);
    free(name);
    name = take(&reference);
    source_edit_connection destination = { .text = name,
                                           .type = state->type,
                                           .symbol = state->id };
    if (!set && r->previous) {
        const source_presentation_origin *target = origin(p, r->previous);
        if (!target || target->kind != SOURCE_PRESENTATION_EXPRESSION) {
            free(name);
            return fail(error, size, "Choose a current value input for Get Variable");
        }
        apush(change->patches, ((text_edit){ target->range.start, target->range.end, xstrdup(name) }));
        change->text = source_edit_check_connection(
            a,
            catalog,
            text_edits_apply(a->text, change->patches, acount(change->patches)),
            &destination,
            declaration,
            target->range.start + 6,
            error,
            size
        );
        free(name);
        if (!change->text) {
            source_edit_changes_free(change);
            return false;
        }
        return true;
    }
    source_edit_connection producer = { 0 };
    char *value = NULL;
    bool self_assignment = false;
    int producer_prefix = 0;
    if (set && r->producer) {
        producer = source_edit_producer(a, catalog, p, r->producer, r->producer_result, error, size);
        if (!producer.text) {
            free(name);
            return false;
        }
        for (int i = 0; i < a->symbol_count; i++) {
            const script_symbol *source = &a->symbols[i];
            if (source->id != producer.symbol ||
                (source->kind != SCRIPT_STATE && source->kind != SCRIPT_SHARED_STATE)) {
                continue;
            }
            char *quoted_name = quoted(catalog, source->name, error, size);
            if (!quoted_name) {
                free(name);
                free(producer.text);
                return false;
            }
            char *reference = NULL;
            append(&reference, "state.%s", quoted_name);
            free(quoted_name);
            free(producer.text);
            producer.text = take(&reference);
            producer_prefix = 6;
            break;
        }
        value = xstrdup(producer.text);
    } else if (set) {
        pin_value zero = { .type = state->type.storage,
                           .container = state->type.container };
        const script_catalog_type
            *type = script_catalog_type_by_id(catalog, state->type.nominal);
        zero.type_name = type ? type->name : NULL;
        if (zero.type == PIN_NAME) {
            zero.str = "";
        }
        value = authoring_source_literal(catalog, state->type, r->value ? r->value : &zero, error, size);
        if (!value && r->value) {
            free(name);
            return false;
        }
        if (!value) {
            value = xstrdup(name);
            self_assignment = true;
        }
    }
    char local[64];
    variable_read_name(a, state->name, local);
    char *fragment = NULL;
    append(&fragment, set ? "%s = %s;" : "%s := %s;", set ? name : local, set ? value : name);
    char *text = take(&fragment);
    authoring_edit_request insert = *r;
    insert.kind = AUTHORING_EDIT_APPEND_BODY;
    insert.fragment = text;
    bool ok = source_edit_structural(a, catalog, p, &insert, change, error, size);
    int first = -1;
    if (ok) {
        assert(acount(change->patches) == 1);
        first = change->patches[0].start + 1;
        change->text = source_edit_check_connection(
            a,
            catalog,
            change->text,
            &destination,
            declaration,
            first + (set ? 0 : (int)strlen(local) + 4) + 6,
            error,
            size
        );
        ok = change->text != NULL;
    }
    if (ok && self_assignment) {
        change->text = source_edit_check_connection(
            a,
            catalog,
            change->text,
            &destination,
            declaration,
            first + (int)strlen(name) + 3 + 6,
            error,
            size
        );
        ok = change->text != NULL;
    }
    if (ok && producer.text) {
        change->text = source_edit_check_connection(
            a,
            catalog,
            change->text,
            &producer,
            origin(p, r->producer),
            first + (int)strlen(name) + 3 + producer_prefix,
            error,
            size
        );
        ok = change->text != NULL;
    }
    free(name);
    free(text);
    free(value);
    free(producer.text);
    if (!ok) {
        source_edit_changes_free(change);
    }
    return ok;
}
bool source_edit_capability(
    const script_analysis *a,
    const script_catalog *catalog,
    const authoring_source_presentation *p,
    const authoring_edit_request *r,
    source_edit_changes *change,
    char *error,
    size_t size
)
{
    if (r->kind == AUTHORING_EDIT_CREATE_VARIABLE_GET ||
        r->kind == AUTHORING_EDIT_CREATE_VARIABLE_SET) {
        return create_variable_access(a, catalog, p, r, change, error, size);
    }
    const script_catalog_capability *c = capability(catalog, r);
    if (r->kind == AUTHORING_EDIT_CREATE_EVENT) {
        int index = r->reference
                      ? script_catalog_find_capability(catalog, r->reference, SCRIPT_CAPABILITY_EVENT)
                      : -1;
        c = index >= 0 ? &catalog->capabilities[index] : NULL;
    }
    script_catalog_capability authored = { 0 };
    script_catalog_parameter *owned_parameters = NULL, *owned_results = NULL;
    if (!c && r->kind == AUTHORING_EDIT_CREATE_CAPABILITY && r->capability_name) {
        const script_function *function = NULL;
        for (int i = 0; i < a->function_count; i++) {
            const script_function *f = &a->functions[i];
            if (f->kind == SCRIPT_FUNCTION_AUTHORED && f->symbol >= 0 &&
                !strcmp(a->symbols[f->symbol].name, r->capability_name)) {
                if (function) {
                    return fail(error, size, "Callable name is ambiguous");
                }
                function = f;
            }
        }
        if (function && function->parameter_count <= MAX_CREATE_PARAMETERS &&
            function->result_count <= MAX_CREATE_PARAMETERS) {
            const script_symbol *symbol = &a->symbols[function->symbol];
            authored = (script_catalog_capability){
                .id = symbol->id,
                .name = symbol->name,
                .qualified_name = symbol->name,
                .kind = symbol->kind == SCRIPT_MACRO ? SCRIPT_CAPABILITY_ACTION
                                                     : SCRIPT_CAPABILITY_FUNCTION,
                .parameter_count = function->parameter_count,
                .result_count = function->result_count,
                .effects = function->effects,
                .pure = function->pure
            };
            for (int i = 0; i < function->parameter_count; i++) {
                const script_symbol *parameter = &a->symbols[function->parameters[i]];
                apush(
                    owned_parameters,
                    ((script_catalog_parameter){ .name = parameter->name,
                                                 .unit = parameter->unit,
                                                 .type = parameter->type,
                                                 .has_default = parameter->initializer >= 0 })
                );
            }
            for (int i = 0; i < function->result_count; i++) {
                const script_symbol *result = &a->symbols[function->results[i]];
                apush(
                    owned_results,
                    ((script_catalog_parameter){ .name = result->name,
                                                 .unit = result->unit,
                                                 .type = result->type })
                );
            }
            authored.parameters = owned_parameters;
            authored.results = owned_results;
            c = &authored;
        }
    }
    if (!c || !c->name || c->parameter_count < 0 ||
        c->parameter_count > MAX_CREATE_PARAMETERS || c->result_count < 0 ||
        c->result_count > MAX_CREATE_PARAMETERS ||
        (c->parameter_count && !c->parameters) || (c->result_count && !c->results) ||
        c->kind == SCRIPT_CAPABILITY_PROTOTYPE) {
        afree(owned_parameters);
        afree(owned_results);
        return fail(error, size, "Choose one declared callable or event capability");
    }
    script_catalog_capability selected = *c;
    script_catalog_parameter field_result = { 0 };
    script_catalog_parameter *parameters = NULL;
    const script_catalog_capability *event = NULL;
    if (c->event_sender) {
        int index = r->reference
                      ? script_catalog_find_capability(catalog, r->reference, SCRIPT_CAPABILITY_EVENT)
                      : -1;
        if (index < 0) {
            return fail(error, size, "Choose the declared event payload to send");
        }
        event = &catalog->capabilities[index];
        if (event->parameter_count < 0 ||
            event->parameter_count > MAX_CREATE_PARAMETERS - c->parameter_count) {
            return fail(error, size, "Event payload exceeds the input limit");
        }
        for (int i = 0; i < c->parameter_count; i++) {
            apush(parameters, c->parameters[i]);
        }
        for (int i = 0; i < event->parameter_count; i++) {
            apush(parameters, event->parameters[i]);
        }
        selected.parameters = parameters;
        selected.parameter_count = acount(parameters);
        c = &selected;
    }
    const char *call_name = c->qualified_name && strchr(c->qualified_name, '.')
                              ? c->qualified_name
                              : c->name;
    char *callee = quoted(catalog, call_name, error, size);
    if (!callee) {
        afree(parameters);
        afree(owned_parameters);
        afree(owned_results);
        return false;
    }
    char *call = NULL, *fragment = NULL;
    char **inputs = NULL;
    asetlen(inputs, c->parameter_count);
    for (int i = 0; i < c->parameter_count; i++) {
        inputs[i] = NULL;
    }
    source_edit_connection producer = { 0 };
    bool ok = false, supplied = false;
    int producer_call = -1, producer_fragment = -1, producer_parameter = -1;
    if (r->producer) {
        producer = source_edit_producer(a, catalog, p, r->producer, r->producer_result, error, size);
        if (!producer.text) {
            goto done;
        }
    }
    char name[64];
    unique_name(a, name, sizeof name);
    if (c->kind == SCRIPT_CAPABILITY_EVENT) {
        if (r->producer || r->previous || r->origin) {
            fail(error, size, "Create event handlers in the declaration body");
            goto done;
        }
        if (!handler_header(catalog, callee, c, &fragment, error, size)) {
            goto done;
        }
    } else {
        if (c->state_access != SCRIPT_STATE_ACCESS_NONE) {
            fail(error, size, "Read or assign the state by name");
            goto done;
        }
        if (event) {
            char *event_name = quoted(catalog, event->name, error, size);
            if (!event_name) {
                goto done;
            }
            append(&call, "emit %s", event_name);
            free(event_name);
        } else {
            append(&call, "%s", callee);
        }
        append(&call, "(");
        int count = 0;
        bool positional = true;
        for (int i = 0; i < c->parameter_count; i++) {
            const script_catalog_parameter *parameter = &c->parameters[i];
            char *value = NULL;
            if (parameter->reference_kind != SCRIPT_REFERENCE_NONE && r->reference) {
                value = reference(catalog, parameter, r->reference, error, size);
                if (!value) {
                    goto done;
                }
            } else if (
                producer.text && !supplied &&
                script_catalog_type_assignable(
                    catalog,
                    parameter->type,
                    producer.type
                )
            ) {
                value = xstrdup(producer.text);
                supplied = true;
                producer_parameter = i;
            } else if (
                !parameter->has_default && !parameter->default_owner &&
                !(parameter->type.flags & SCRIPT_TYPE_OPTIONAL)
            ) {
                fail(error, size, "Supply the required capability input before creating this call");
                goto done;
            }
            if (!value) {
                positional = false;
                continue;
            }
            inputs[i] = value;
            char *argument = positional
                               ? xstrdup("")
                               : quoted(catalog, parameter->name, error, size);
            if (!argument) {
                goto done;
            }
            if (producer.text && !strcmp(value, producer.text) && supplied &&
                producer_call < 0) {
                producer_call = acount(call) + (count ? 2 : 0) +
                                (positional ? 0 : (int)strlen(argument) + 2) +
                                ((parameter->type.flags & SCRIPT_TYPE_REFERENCE) ? 4 : 0);
            }
            append(
                &call,
                "%s%s%s%s%s",
                count++ ? ", " : "",
                argument,
                positional ? "" : ": ",
                (parameter->type.flags & SCRIPT_TYPE_REFERENCE) ? "ref " : "",
                value
            );
            free(argument);
        }
        if (producer.text && !supplied) {
            fail(error, size, "Producer has no compatible capability input");
            goto done;
        }
        append(&call, ")");
        char *expression = take(&call);
        bool component_path = c->component &&
                              (c->component_access == SCRIPT_COMPONENT_ACCESS_READ ||
                               c->component_access == SCRIPT_COMPONENT_ACCESS_WRITE ||
                               c->component_access == SCRIPT_COMPONENT_ACCESS_HAS);
        if (component_path) {
            free(expression);
            if (c->component_access == SCRIPT_COMPONENT_ACCESS_WRITE &&
                (!r->origin || r->previous)) {
                fail(error, size, "Create component assignments inside an executable body");
                goto done;
            }
            if (!default_inputs(catalog, c, inputs, error, size)) {
                goto done;
            }
            expression = component_expression(catalog, c, inputs, producer_parameter, &producer_call, error, size);
            if (!expression) {
                goto done;
            }
            if (c->component_access == SCRIPT_COMPONENT_ACCESS_WRITE) {
                selected = *c;
                selected.kind = SCRIPT_CAPABILITY_ACTION;
                selected.result_count = 0;
                c = &selected;
            }
        } else if (
            c != &authored && !c->parameter_count && c->result_count == 1 &&
            c->results[0].type.storage == PIN_HANDLE &&
            c->results[0].type.nominal == catalog->entity_type &&
            c->id == script_catalog_id("capability", "Self")
        ) {
            free(expression);
            expression = xstrdup("self");
        } else if (c->operator_name && *c->operator_name) {
            free(expression);
            if (!default_inputs(catalog, c, inputs, error, size)) {
                goto done;
            }
            bool short_circuit = !strcmp(c->operator_name, "and") ||
                                 !strcmp(c->operator_name, "or") ||
                                 !strcmp(c->operator_name, "&&") ||
                                 !strcmp(c->operator_name, "||");
            const source_presentation_origin *source = origin(p, r->producer);
            if (short_circuit && producer_parameter == 1 && source &&
                source->kind == SOURCE_PRESENTATION_EXPRESSION &&
                (a->expressions[source->record].effects &
                 ~(SCRIPT_EFFECT_READ | SCRIPT_EFFECT_INVOCATION_READ))) {
                fail(error, size, "A short-circuit operator cannot skip an effectful producer");
                goto done;
            }
            expression = operator_expression(c, inputs, producer_parameter, &producer_call, error, size);
            if (!expression) {
                goto done;
            }
        }
        if (r->field) {
            const script_catalog_type *type = script_catalog_type_by_id(
                catalog,
                c->component ? c->component : c->aggregate
            );
            if (!type && r->reference) {
                int index = script_catalog_find_type(catalog, r->reference);
                type = index >= 0 ? &catalog->types[index] : NULL;
            }
            int index = type ? script_catalog_find_field(type, r->field) : -1;
            if (index < 0 || c->component_access != SCRIPT_COMPONENT_ACCESS_READ) {
                free(expression);
                fail(error, size, "Choose a declared component read field");
                goto done;
            }
            char *field = quoted(catalog, type->fields[index].name, error, size);
            if (!field) {
                free(expression);
                goto done;
            }
            append(&call, component_path ? "%s.%s" : "(%s).%s", expression, field);
            if (producer_call >= 0 && !component_path) {
                producer_call++;
            }
            free(expression);
            free(field);
            expression = take(&call);
            field_result = (script_catalog_parameter){
                .name = type->fields[index].name,
                .type = type->fields[index].type
            };
            selected = *c;
            selected.results = &field_result;
            selected.result_count = 1;
            c = &selected;
        }
        if (!r->origin) {
            if (c->kind != SCRIPT_CAPABILITY_ACTION && c->result_count) {
                free(expression);
                fail(error, size, "Create values inside a handler or function body");
                goto done;
            }
            producer_fragment = producer_call + (int)strlen("action") +
                                (int)strlen(name) + 4;
            append(&fragment, "action %s = %s;\n", name, expression);
        } else if (c->kind == SCRIPT_CAPABILITY_ACTION && r->previous) {
            bool waiting = (c->effects & SCRIPT_EFFECT_SUSPEND) != 0;
            if (!c->result_count ||
                (waiting && (c->completion_outcome < 0 ||
                             c->completion_outcome >= c->outcome_count ||
                             !c->outcomes || !c->outcomes[c->completion_outcome]))) {
                free(expression);
                fail(error, size, "Choose an action with a declared completed payload");
                goto done;
            }
            producer_fragment = producer_call + (int)strlen(name) + 4 +
                                (waiting ? 6 : 0);
            append(&fragment, "%s := %s%s;\n", name, waiting ? "await " : "", expression);
        } else if (c->kind == SCRIPT_CAPABILITY_ACTION) {
            producer_fragment = producer_call;
            append(&fragment, "%s;\n", expression);
        } else if (c->result_count) {
            producer_fragment = producer_call + (int)strlen(name) + 4;
            append(&fragment, "%s := %s;\n", name, expression);
        } else {
            producer_fragment = producer_call;
            append(&fragment, "%s;\n", expression);
        }
        free(expression);
    }
    char *created = take(&fragment);
    authoring_edit_request insert = *r;
    insert.kind = r->previous ? AUTHORING_EDIT_INSERT_BEFORE
                : r->origin   ? AUTHORING_EDIT_APPEND_BODY
                              : AUTHORING_EDIT_APPEND_DECLARATIONS;
    if (r->previous) {
        insert.origin = r->previous;
    }
    insert.fragment = created;
    ok = source_edit_structural(a, catalog, p, &insert, change, error, size);
    free(created);
    if (!ok) {
        goto done;
    }
    if (r->previous) {
        const source_presentation_origin *target = origin(p, r->previous);
        if (!target || target->kind != SOURCE_PRESENTATION_EXPRESSION ||
            !c->result_count) {
            fail(error, size, "Connect a created retained result to a current value expression");
            ok = false;
            goto done;
        }
        char *result = NULL;
        if (c->result_count == 1) {
            append(&result, "%s", name);
        } else {
            int selected = -1;
            for (int i = 0; i < c->result_count; i++) {
                if (script_catalog_type_assignable(
                        catalog,
                        target->type,
                        c->results[i].type
                    )) {
                    if (selected >= 0) {
                        fail(error, size, "Choose which compatible result to connect");
                        ok = false;
                        goto done;
                    }
                    selected = i;
                }
            }
            if (selected < 0) {
                fail(error, size, "Created capability has no compatible output");
                ok = false;
                goto done;
            }
            char *field = quoted(catalog, c->results[selected].name, error, size);
            if (!field) {
                ok = false;
                goto done;
            }
            append(&result, "%s.%s", name, field);
            free(field);
        }
        char *replacement = take(&result);
        apush(change->patches, ((text_edit){ target->range.start, target->range.end, replacement }));
    }
    free(change->text);
    change->text = text_edits_apply(a->text, change->patches, acount(change->patches));
    if (!change->text) {
        fail(error, size, "Created call and target edits overlap");
        ok = false;
        goto done;
    }
    if (producer.text) {
        int destination = -1;
        for (int i = 0; i < acount(change->patches); i++) {
            const text_edit *patch = &change->patches[i];
            if (patch->start != patch->end) {
                continue;
            }
            destination = patch->start + 1 + producer_fragment;
            for (int j = 0; j < acount(change->patches); j++) {
                if (change->patches[j].end <= patch->start && j != i) {
                    destination += (int)strlen(change->patches[j].text) -
                                   (change->patches[j].end - change->patches[j].start);
                }
            }
            break;
        }
        change->text = source_edit_check_connection(
            a,
            catalog,
            change->text,
            &producer,
            origin(p, r->producer),
            destination,
            error,
            size
        );
        ok = change->text != NULL;
    }
done:
    for (int i = 0; i < acount(inputs); i++) {
        free(inputs[i]);
    }
    afree(inputs);
    free(callee);
    afree(parameters);
    afree(owned_parameters);
    afree(owned_results);
    free(producer.text);
    afree(call);
    afree(fragment);
    if (!ok) {
        source_edit_changes_free(change);
    }
    return ok;
}
