#include "authoring/source_edit_internal.h"
#include "authoring/source_literal.h"
#include "support/array.h"
#include "support/xalloc.h"

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
    return authoring_source_literal(
        catalog,
        (script_type){ .storage = PIN_STR },
        &(pin_value){ .type = PIN_STR, .str = name },
        error,
        size
    );
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
    if (p->reference_kind == SCRIPT_REFERENCE_COMPONENT) {
        int index = script_catalog_find_type(catalog, name);
        if (index < 0 || !catalog->types[index].component) {
            fail(error, size, "Choose a registered component reference");
            return NULL;
        }
        return quoted(catalog, catalog->types[index].name, error, size);
    }
    int index = script_catalog_find_capability(catalog, name, SCRIPT_CAPABILITY_PROTOTYPE);
    if (index < 0) {
        fail(error, size, "Choose a registered prototype reference");
        return NULL;
    }
    return quoted(catalog, catalog->capabilities[index].name, error, size);
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
                                                 .type = parameter->type,
                                                 .has_default = parameter->initializer >= 0 })
                );
            }
            for (int i = 0; i < function->result_count; i++) {
                const script_symbol *result = &a->symbols[function->results[i]];
                apush(
                    owned_results,
                    ((script_catalog_parameter){ .name = result->name,
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
    char *callee = quoted(catalog, c->qualified_name ? c->qualified_name : c->name, error, size);
    if (!callee) {
        afree(parameters);
        afree(owned_parameters);
        afree(owned_results);
        return false;
    }
    char *call = NULL, *fragment = NULL;
    source_edit_connection producer = { 0 };
    bool ok = false, supplied = false;
    int producer_call = -1, producer_fragment = -1;
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
        append(&fragment, "on %s as %s {}\n", callee, name);
    } else {
        append(&call, "%s", callee);
        if (c->state_access != SCRIPT_STATE_ACCESS_NONE) {
            if (!r->reference) {
                fail(error, size, "Choose the declared state for this access");
                goto done;
            }
            char *state = quoted(catalog, r->reference, error, size);
            if (!state) {
                goto done;
            }
            append(&call, "<%s>", state);
            free(state);
        }
        if (event) {
            char *event_name = quoted(catalog, event->name, error, size);
            if (!event_name) {
                goto done;
            }
            append(&call, "<%s>", event_name);
            free(event_name);
        }
        append(&call, "(");
        int count = 0;
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
            } else if (
                !parameter->has_default && !parameter->default_owner &&
                !(parameter->type.flags & SCRIPT_TYPE_OPTIONAL)
            ) {
                fail(error, size, "Supply the required capability input before creating this call");
                goto done;
            }
            if (!value) {
                continue;
            }
            char *argument = quoted(catalog, parameter->name, error, size);
            if (!argument) {
                free(value);
                goto done;
            }
            if (producer.text && !strcmp(value, producer.text) && supplied &&
                producer_call < 0) {
                producer_call = acount(call) + (count ? 2 : 0) +
                                (int)strlen(argument) + 2;
            }
            append(&call, "%s%s: %s", count++ ? ", " : "", argument, value);
            free(argument);
            free(value);
        }
        if (producer.text && !supplied) {
            fail(error, size, "Producer has no compatible capability input");
            goto done;
        }
        append(&call, ")");
        char *expression = take(&call);
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
            append(&call, "(%s).%s", expression, field);
            if (producer_call >= 0) {
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
            const char *storage = c->kind == SCRIPT_CAPABILITY_ACTION || !c->result_count
                                    ? "action"
                                    : "local";
            producer_fragment = producer_call + (int)strlen(storage) +
                                (int)strlen(name) + 4;
            append(&fragment, "%s %s = %s;\n", storage, name, expression);
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
