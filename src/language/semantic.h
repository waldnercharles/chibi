#ifndef CHIBI_LANGUAGE_SEMANTIC_H
#define CHIBI_LANGUAGE_SEMANTIC_H

#include "capabilities/catalog.h"
#include "language/syntax.h"
#include "support/arena.h"

typedef enum
{
    SCRIPT_GLOBAL,
    SCRIPT_FUNCTION,
    SCRIPT_MACRO,
    SCRIPT_EVENT,
    SCRIPT_STATE,
    SCRIPT_SHARED_STATE,
    SCRIPT_PARAMETER,
    SCRIPT_RESULT,
    SCRIPT_LOCAL,
    SCRIPT_LAZY,
    SCRIPT_ACTION,
    SCRIPT_COMPONENT,
    SCRIPT_LABEL,
    SCRIPT_TYPE_PARAMETER,
} script_symbol_kind;
typedef struct
{
    uint64_t id, capability;
    char *name;
    script_symbol_kind kind;
    script_type type;
    script_range declaration;
    int scope, initializer, function;
    bool writable, disabled, resolving, outcome, lazy_local;
} script_symbol;
typedef enum
{
    SCRIPT_READ,
    SCRIPT_WRITE,
    SCRIPT_CALL,
    SCRIPT_HANDLE,
    SCRIPT_TYPE_USE
} script_use_kind;
typedef struct
{
    script_range range;
    int symbol;
    script_use_kind kind;
    uint64_t member;
} script_use;
typedef struct
{
    int parent, function;
    script_range range;
} script_scope;
typedef enum
{
    SCRIPT_EXPR_ERROR,
    SCRIPT_EXPR_CONSTANT,
    SCRIPT_EXPR_SYMBOL,
    SCRIPT_EXPR_UNARY,
    SCRIPT_EXPR_BINARY,
    SCRIPT_EXPR_CAST,
    SCRIPT_EXPR_CALL,
    SCRIPT_EXPR_MEMBER,
    SCRIPT_EXPR_COMPONENT,
    SCRIPT_EXPR_INDEX,
    SCRIPT_EXPR_ARRAY,
    SCRIPT_EXPR_STRUCT,
    SCRIPT_EXPR_NIL,
    SCRIPT_EXPR_FALLBACK,
    SCRIPT_EXPR_STORE,
} script_expression_kind;
typedef enum
{
    SCRIPT_OP_NONE,
    SCRIPT_OP_ADD,
    SCRIPT_OP_SUB,
    SCRIPT_OP_MUL,
    SCRIPT_OP_DIV,
    SCRIPT_OP_MOD,
    SCRIPT_OP_NEG,
    SCRIPT_OP_NOT,
    SCRIPT_OP_AND,
    SCRIPT_OP_OR,
    SCRIPT_OP_EQ,
    SCRIPT_OP_NE,
    SCRIPT_OP_LT,
    SCRIPT_OP_LE,
    SCRIPT_OP_GT,
    SCRIPT_OP_GE,
    SCRIPT_OP_BIT_AND,
    SCRIPT_OP_BIT_OR,
    SCRIPT_OP_BIT_XOR,
    SCRIPT_OP_SHIFT_LEFT,
    SCRIPT_OP_SHIFT_RIGHT,
    SCRIPT_OP_BIT_NOT,
    SCRIPT_OP_REFERENCE,
} script_operator;
typedef struct
{
    int value, parameter;
    uint64_t member;
    script_range range;
} script_argument;
typedef struct
{
    script_expression_kind kind;
    script_operator op;
    script_type type;
    script_range range;
    uint64_t capability, member, read, write, instance;
    int symbol, left, right, entry;
    script_argument *arguments;
    int argument_count;
    script_type *results;
    int result_count;
    pin_value constant;
    size_t offset;
    unsigned effects;
    bool writable, awaited, constant_snapshot, partial;
} script_expression;
typedef enum
{
    SCRIPT_STMT_BLOCK,
    SCRIPT_STMT_DECLARE,
    SCRIPT_STMT_ASSIGN,
    SCRIPT_STMT_EXPRESSION,
    SCRIPT_STMT_IF,
    SCRIPT_STMT_WHILE,
    SCRIPT_STMT_FOR,
    SCRIPT_STMT_FOREACH,
    SCRIPT_STMT_BREAK,
    SCRIPT_STMT_CONTINUE,
    SCRIPT_STMT_RETURN,
    SCRIPT_STMT_LABEL,
    SCRIPT_STMT_GOTO,
    SCRIPT_STMT_SEQUENCE,
    SCRIPT_STMT_MATCH,
    SCRIPT_STMT_CASE,
} script_statement_kind;
typedef struct
{
    script_statement_kind kind;
    script_range range;
    int scope, symbol, expression, second, third, body, alternative;
    int *children;
    int child_count;
    script_operator op;
    unsigned effects;
} script_statement;
typedef enum
{
    SCRIPT_FUNCTION_AUTHORED,
    SCRIPT_FUNCTION_HANDLER,
    SCRIPT_FUNCTION_CONTINUATION,
    SCRIPT_FUNCTION_SYSTEM,
    SCRIPT_FUNCTION_MACRO_ENTRY,
} script_function_kind;
typedef struct
{
    script_function_kind kind;
    int symbol, scope, body, receiver, parent;
    int *parameters, *results;
    int *type_parameters;
    int parameter_count, result_count;
    int type_parameter_count;
    int depth_limit;
    uint64_t event, action;
    char *entry;
    script_range range;
    unsigned effects;
    bool pure, disabled;
} script_function;
typedef enum
{
    SCRIPT_QUERY_HAS,
    SCRIPT_QUERY_ALL,
    SCRIPT_QUERY_ANY,
    SCRIPT_QUERY_NOT
} script_query_kind;
typedef struct
{
    script_query_kind kind;
    uint64_t component;
    int *children;
    int child_count;
    script_range range;
} script_query;
typedef struct
{
    int target, value;
    script_range range;
    uint64_t *reads, *writes;
    int read_count, write_count;
} script_binding;
typedef enum
{
    SCRIPT_PROGRAM_BEHAVIOR,
    SCRIPT_PROGRAM_LIBRARY,
    SCRIPT_PROGRAM_SYSTEM
} script_program_kind;
/* The resolved program owns text, names, constants, origins and typed records.
 * Indices refer only to this immutable program, never to syntax/graph objects.
 * A failed analysis retains facts and diagnostics but is not executable. */
typedef struct
{
    char *text, *name, *identity;
    arena constants;
    script_program_kind kind;
    script_symbol *symbols;
    script_use *uses;
    script_scope *scopes;
    script_expression *expressions;
    script_statement *statements;
    script_function *functions;
    script_query *queries;
    script_binding *bindings;
    script_diagnostic *diagnostics;
    int symbol_count, use_count, scope_count, expression_count, statement_count,
        function_count, query_count, binding_count, diagnostic_count;
    int query;
    int owner_symbol;
    uint64_t owner_type;
    bool valid;
} script_analysis;

script_analysis *script_analyze(const script_syntax *, const script_catalog *);
/* A stable document key is independent of its display path and declared name.
 * Project callers use a project-relative asset key for reproducible identities. */
script_analysis *script_analyze_document(const script_syntax *, const script_catalog *, const char *key);
/* Signature discovery does not resolve bodies; useful for project catalogs. */
script_analysis *script_declarations(const script_syntax *, const script_catalog *);
script_analysis *script_declarations_document(
    const script_syntax *,
    const script_catalog *,
    const char *key
);
void script_analysis_free(script_analysis *);
const script_symbol *script_symbol_at(const script_analysis *, int offset);
const script_use *script_use_at(const script_analysis *, int offset);
#endif
