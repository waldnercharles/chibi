#include "language/nominal_binding.h"

#include <stdio.h>
#include <string.h>

static const char *own(arena *storage, const char *text)
{
    size_t bytes = strlen(text) + 1;
    char *copy = arena_alloc(storage, bytes);
    memcpy(copy, text, bytes);
    return copy;
}
bool script_nominal_allowed(script_nominal_context context, script_nominal_kind kind)
{
    return kind != SCRIPT_NOMINAL_RECORD ||
           (context.program_kind == SCRIPT_PROGRAM_LIBRARY &&
            context.lexical.function < 0);
}
script_nominal_declaration script_nominal_declare(
    arena *storage,
    const script_lexical_facts *facts,
    script_nominal_context context,
    script_nominal_request request
)
{
    script_nominal_declaration out = { .existing = -1 };
    bool record = request.kind == SCRIPT_NOMINAL_RECORD;
    if (!script_nominal_allowed(context, request.kind)) {
        out.diagnostics[0].range = request.range;
        snprintf(out.diagnostics[0].message, sizeof out.diagnostics[0].message, "Records must be declared in a library");
        out.diagnostic_count = 1;
        return out;
    }
    out.declare = true;
    out.declaration_type = record ? (script_type){ .storage = PIN_STRUCT }
                                  : (script_type){ .storage = PIN_UNION,
                                                   .flags = SCRIPT_TYPE_UNION };
    script_lexical_declaration declaration = script_lexical_declare(
        facts,
        context.lexical,
        request.name_range,
        request.name,
        SCRIPT_TYPE
    );
    out.existing = declaration.existing;
    out.diagnostic_count = declaration.diagnostic_count;
    memcpy(
        out.diagnostics,
        declaration.diagnostics,
        (size_t)declaration.diagnostic_count * sizeof *out.diagnostics
    );
    const char *name = request.name;
    script_type type = out.declaration_type;
    out.id = declaration.id;
    if (out.existing >= 0) {
        const script_symbol *symbol = &facts->symbols[out.existing];
        out.id = symbol->id;
        if (symbol->kind != SCRIPT_TYPE ||
            (!record && symbol->type.storage != PIN_UNION) ||
            symbol->declaration.document != request.name_range.document ||
            symbol->declaration.start != request.name_range.start) {
            return out;
        }
        name = symbol->name;
        type = symbol->type;
    }
    out.publish = true;
    type.nominal = out.id;
    out.type = type;
    out.source_name = own(storage, name);
    out.authored_record = record;
    if (record) {
        size_t bytes = strlen(context.program_name) + strlen(name) + 2;
        char *qualified = arena_alloc(storage, bytes);
        snprintf(qualified, bytes, "%s.%s", context.program_name, name);
        out.name = qualified;
    } else {
        out.name = own(storage, name);
        out.size = context.pointer_size;
    }
    return out;
}
