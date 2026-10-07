#ifndef CHIBI_CODE_SOURCE_INTELLIGENCE_H
#define CHIBI_CODE_SOURCE_INTELLIGENCE_H

#include "assets/asset_source.h"
#include "graph/model/pin.h"
#include "support/text_edit.h"

#include <stdint.h>

typedef struct code_source code_source;
typedef enum
{
    CODE_SYMBOL_STATE,
    CODE_SYMBOL_LOCAL,
    CODE_SYMBOL_PARAMETER,
    CODE_SYMBOL_FUNCTION,
    CODE_SYMBOL_MACRO,
    CODE_SYMBOL_EVENT,
    CODE_SYMBOL_COMPONENT,
    CODE_SYMBOL_FIELD,
    CODE_SYMBOL_TYPE,
    CODE_SYMBOL_LABEL
} code_symbol_kind;
typedef enum
{
    CODE_USE_READ = 1,
    CODE_USE_WRITE = 2,
    CODE_USE_CALL = 4
} code_use_role;
typedef struct
{
    pin_type type;
    pin_container container;
    const char *type_name;
} code_semantic_type;
typedef struct
{
    uint64_t id, owner_id, definition_id;
    code_symbol_kind kind;
    const char *name, *signature, *description;
    /* NULL owns native identities in this source; otherwise identifies the
     * imported asset whose graph/node/pin identities must remain untouched. */
    const char *asset_name;
    code_semantic_type type;
    int start, end, scope_start, scope_end;
    uint64_t graph_id, node_id, pin_id;
    bool renameable;
    int completion_priority;
} code_semantic_symbol;
typedef struct
{
    uint64_t symbol_id;
    int start, end;
    unsigned role;
} code_semantic_use;
typedef struct
{
    uint64_t caller_id, target_id;
    int start, end;
} code_semantic_call;
typedef struct
{
    /* Parser emits the exact insertion/replacement range and valid kinds.
     * Optional expected type filters values; receiver_id restricts members.
     * numeric_type owns a cast target slot over broader recovery contexts. */
    int start, end;
    unsigned kinds;
    uint64_t receiver_id;
    bool typed, native_callables, numeric_type, qualified_callables,
        reference_value, ecs_member, ecs_event;
    code_semantic_type expected;
} code_completion_context;
#define CODE_SYMBOL_MASK(kind) (1u << (kind))

char *code_intelligence_callable_signature(
    const char *name,
    const pin_spec *inputs,
    int input_count,
    const pin_spec *outputs,
    int output_count
);
/* Facts come from parser name/type resolution, never a parallel lexical parse.
 * Strings are copied. A source owns its facts until clear/free. IDs are unique
 * within one source generation; graph/node/pin IDs retain native identities. */
void code_intelligence_clear(code_source *source);
/* Replacing an existing ID refreshes deferred type/signature resolution. */
void code_intelligence_symbol(code_source *source, const code_semantic_symbol *symbol);
void code_intelligence_use(code_source *source, code_semantic_use use);
void code_intelligence_call(code_source *source, code_semantic_call call);
void code_intelligence_context(code_source *source, const code_completion_context *context);
void code_intelligence_location(
    code_source *source,
    uint64_t id,
    uint64_t graph_id,
    uint64_t node_id,
    uint64_t pin_id
);
/* Metadata restoration remaps native identities without changing semantic IDs. */
void code_intelligence_remap_native(
    code_source *source,
    const uint64_t *from,
    const uint64_t *to,
    int count
);
/* Context-specific grammar/callable proposals, emitted by the parser using its
 * real callable grammar. detail/insert strings are copied and owned by source. */
void code_intelligence_offer(
    code_source *source,
    int context_start,
    const char *label,
    const char *detail,
    const char *insert,
    code_completion_kind kind,
    const code_semantic_type *type
);

void code_intelligence_offer_qualified(
    code_source *source,
    int context_start,
    const char *label,
    const char *detail,
    const char *insert
);

/* Borrowed declaration list supports outline and resolving call tree labels. */
const code_semantic_symbol *code_intelligence_symbols(const code_source *source, int *count);
const code_semantic_symbol *code_intelligence_symbol_by_id(const code_source *source, uint64_t id);
const code_semantic_symbol *code_intelligence_symbol_at(const code_source *source, int offset);
bool code_intelligence_definition(const code_source *source, int offset, asset_source_span *range);
asset_source_span *code_intelligence_references(const code_source *source, int offset);
asset_source_span *code_intelligence_references_for(const code_source *source, uint64_t id);
char *code_intelligence_hover(const code_source *source, int offset);
void code_intelligence_complete(const code_source *source, int offset, asset_source_completions *out);
/* Returns owned replacement source; rejects capture, reserved/invalid names and
 * symbols without an editable source declaration. error explains refusal. */
char *code_intelligence_rename(
    const code_source *source,
    int offset,
    const char *name,
    char *error,
    size_t size
);
/* Resolved rename with separate metadata preserving live-state and graph IDs. */
char *code_intelligence_rename_with_metadata(
    const code_source *source,
    int offset,
    const char *name,
    char **metadata,
    char *error,
    size_t size
);
/* Caller frees returned arrays with afree; entries retain source-owned strings. */
code_semantic_call *code_intelligence_calls_for(const code_source *source, uint64_t id, bool callers);
code_semantic_call *code_intelligence_calls(const code_source *source, int offset, bool callers);
asset_source_span *code_intelligence_highlights(const code_source *source);

#endif
