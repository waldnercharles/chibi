#ifndef CHIBI_CODE_PROGRAM_INTERNAL_H
#define CHIBI_CODE_PROGRAM_INTERNAL_H
#include "assets/code_calls.h"
#include "assets/code_program.h"
#include "support/asset_value.h"
#include "support/code_lex.h"
typedef struct
{
    code_call_port port;
    char *text;
    int start, end, name_start, name_end;
    bool annotated, has_resolved, positional;
    int ordinal;
    code_call_argument resolved;
} argument;
typedef struct declaration declaration;
typedef struct scope scope;
typedef enum
{
    CODE_STMT_CALL,
    CODE_STMT_EXPRESSION,
    CODE_STMT_LOCAL,
    CODE_STMT_IF,
    CODE_STMT_WHILE,
    CODE_STMT_FOR,
    CODE_STMT_FOREACH,
    CODE_STMT_RETURN,
    CODE_STMT_LABEL,
    CODE_STMT_GOTO,
    CODE_STMT_BREAK,
    CODE_STMT_CONTINUE,
    CODE_STMT_SEQUENCE,
    CODE_STMT_MATCH,
    CODE_STMT_BLOCK
} code_program_statement_kind;
typedef struct statement
{
    code_program_statement_kind kind;
    char *target, *method;
    char *result_name;
    int result_start, result_end;
    declaration *result_alias;
    argument *arguments;
    char *text, *second, *third, *alias;
    int value_start, second_start, third_start;
    int target_start, target_end, method_start, method_end;
    struct statement *body, *alternate;
    node *native, *sequence;
    bool awaited, label_alias, engine;
    int start, end;
} statement;
typedef struct
{
    char *source, *outcome;
    int source_start, source_end, outcome_start, outcome_end;
    declaration **aliases;
    statement *body;
    int start, end;
} continuation;
struct declaration
{
    char *name, *expression;
    int expression_start;
    code_call_spec call;
    argument *arguments;
    code_call_port *results;
    node *native;
    code_call_output expression_output;
    int start, end, building;
    int call_start, call_end, reference_start, reference_end;
    uint64_t symbol;
    bool disabled, lazy, result_local;
};
typedef struct
{
    char *name, *text;
    int start, end, value_start;
    uint64_t id;
    code_call_argument resolved;
    bool has_resolved, building;
} local_declaration;
typedef struct
{
    const char *name;
    node *node;
    statement *statement;
} code_program_label;
typedef struct
{
    const char *name, *key;
    const type_desc *type;
    bool optional, narrowed;
    int start, end;
} code_component_binding;
struct scope
{
    graph *native;
    int start, end, name_start, name_end;
    uint64_t symbol;
    bool entries_defined, exits_defined;
    argument *inputs, *outputs;
    code_call_port *type_parameters;
    local_declaration *locals;
    code_component_binding *components;
    unsigned narrowing_revision;
    declaration **declarations;
    continuation *continuations;
    argument *returns;
    code_program_label *labels;
};
typedef struct
{
    char *name;
    code_call_port type;
    uint64_t native_id;
    int start, end;
    uint64_t symbol;
} state_declaration;
typedef struct
{
    char *name, *text;
    int start, end;
    code_call_argument resolved;
    pin_type type;
    bool contextual_number;
} code_expression_read;
typedef struct
{
    node *native;
    char *text;
    int start, end;
    code_expression_read *reads;
} code_expression_source;
typedef struct
{
    node *native;
    int *explicit_inputs;
} code_program_call_inputs;
struct code_program
{
    scope **scopes;
    state_declaration *states;
    code_expression_source *expressions;
    code_program_call_inputs *call_inputs;
    char **strings;
    av_doc *values;
    av_value *query;
    int query_start, query_end;
    bool system, system_header;
};
typedef struct
{
    code_source *source;
    code_program *program;
    code_lexer lexer;
    code_token token;
    scope *scope;
    behavior_asset *asset;
    int depth, base;
    graph_expression_mode expression_mode;
    code_call_port *type_parameters;
    const code_call_spec *completion_call;
    bool numeric_type; /* Numeric-only type proposals for the cast target. */
} parser;

int code_components_value(parser *, scope *, const char *, int, code_call_port *, code_call_argument *);
int code_components_condition(
    parser *,
    scope *,
    const char *,
    int,
    code_call_argument *,
    code_component_binding **,
    bool *
);
int code_components_assignment(parser *, scope *, const char *, int, code_call_argument *);
bool code_reactive_parse_bind(parser *);
bool code_reactive_parse_event(parser *);
void code_reactive_finish(parser *);
bool code_program_system_declaration(parser *);
void code_program_next(parser *p);
bool code_program_take(parser *p, const char *word);
bool code_program_expect(parser *p, const char *word);
char *code_program_name(parser *p);
char *code_program_reference_name(parser *p);
char *code_program_capture(parser *p, int *start, int *end);
argument *code_program_arguments(parser *p, bool results);
bool code_program_type_token(parser *p);
bool code_program_atomic_value(const char *text);
code_call_port code_program_type(parser *p);
node *code_program_call(parser *p, scope *s, code_call_spec call, argument *arguments, int start, int end);
void code_program_record_inputs(
    code_program *program,
    node *native,
    const code_call_argument *arguments,
    int count
);
bool code_program_value(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    code_call_port *expected,
    code_call_argument *out
);
declaration *code_program_declaration(scope *s, const char *name);
node *code_program_build(parser *p, scope *s, declaration *d);
statement *code_statement_parse_block(parser *p);
statement *code_statement_parse_body(parser *p);
statement code_statement_parse_match(parser *p);
node *code_statement_match_node(parser *p, scope *s, statement *statement, int **outputs, int *default_output);
void code_statement_lower_block(parser *p, scope *s, node *from, int output, statement *body);
/* Private incomplete candidate: resolve loop inputs/alias without execution wiring. */
bool code_statement_probe_loop(parser *p, scope *s, statement *stmt, declaration *alias);
node *code_statement_probe_call(parser *p, scope *s, statement *stmt);
bool code_statement_expression(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    code_call_port *expected,
    code_call_argument *out
);
bool code_statement_value_type(parser *p, scope *s, const code_call_argument *value, code_call_port *port);
/* -1: ordinary expression; 0: invalid method; 1: resolved static method. */
int code_statement_method(parser *, scope *, const char *, int, code_call_argument *);
bool code_statement_math_call(parser *p, const char *text);
void code_statement_free(statement *body);
node *code_program_statement_node(scope *s, statement *statement);
void code_statement_prepare_labels(parser *p, scope *s);
void code_statement_finish_labels(parser *p, scope *s);
char *code_statement_display_expression(
    const code_program *program,
    const behavior_asset *asset,
    const node *expression
);
char *code_statement_resolve_expression(
    const code_program *program,
    const node *expression,
    const char *text
);
void code_statement_parse_local(parser *p);
void code_statement_lower_locals(parser *p, scope *s);
void code_statement_order_locals(scope *s);
bool code_statement_lower_local(parser *p, scope *s, local_declaration *local);
void code_program_intelligence(parser *p);
void code_program_partial_intelligence(parser *p);
void code_program_complete_callable(parser *p, int start, int end);
void code_program_complete_qualified(parser *p, const char *asset, int start, int end);
void code_program_complete_type(parser *p, int start, int end);
void code_program_complete_cast(parser *p);
void code_program_complete_event(parser *p, int start, int end);
void code_program_complete_vector_method(parser *, int, int);
void code_program_complete_ecs_path(parser *, scope *, const char *, int);
void code_program_complete_ecs_event(parser *, scope *, const char *, int);
void code_program_complete_ecs_value(parser *, scope *, const char *, int, const code_call_port *);
void code_program_complete_value(parser *p, scope *s, int start, int end, const code_call_port *expected);
void code_program_complete_arguments(parser *p, const code_call_spec *call, int start, int end);
bool code_program_argument_port(parser *p, const code_call_spec *call, const char *name, code_call_port *out);
bool code_program_positional_port(parser *p, const code_call_spec *call, int ordinal, code_call_port *out);
void code_program_resolved_use(
    parser *p,
    scope *s,
    const char *atom,
    int offset,
    const code_call_argument *value,
    unsigned role
);
#endif
