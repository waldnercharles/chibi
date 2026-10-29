#ifndef CHIBI_LANGUAGE_SEMANTIC_H
#define CHIBI_LANGUAGE_SEMANTIC_H

#include "capabilities/catalog.h"
/* Includes nested collection/union payload schemas, even when values are empty. */
bool script_type_contains_tasks(const script_catalog *, script_type);
#include "language/syntax.h"
#include "support/arena.h"
#include "types/scalar.h"

/* One language-wide recursion budget; authored source cannot override it. */
#define SCRIPT_CALL_DEPTH_LIMIT 64

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
    SCRIPT_ACTION,
    SCRIPT_COMPONENT,
    SCRIPT_LABEL,
    SCRIPT_TYPE_PARAMETER,
    SCRIPT_TYPE,
    SCRIPT_CONSTANT,
    SCRIPT_QUERY_VARIABLE,
} script_symbol_kind;
typedef struct
{
    uint64_t id, capability;
    char *name;
    char *unit; /* Owned erased parameter/result annotation; NULL means unknown. */
    script_symbol_kind kind;
    script_type type;
    script_range declaration;
    int scope, initializer, function;
    bool writable, disabled, resolving, outcome;
    bool pub;         /* Package declaration other packages import. */
    bool addressed;   /* A managed cell owns this local from its declaration. */
    bool boxed;       /* A task keeps this local by reference. */
    bool mixed_tasks; /* Holds tasks of different functions. */
    unsigned mutations;           /* Invalidates proofs after assignments. */
    uint64_t source_relationship; /* Query component provider; zero is self. */
    int source_traversal;         /* script_query_source */
    int source_variable;          /* Query row variable; zero is self. */
    int query_description;        /* One-based descriptor for a row binding. */
    int query_field;
    int query_row_owner; /* One-based row symbol for a hidden field proof. */
    script_type component_type; /* Provider type before pointer wrapping. */
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
    SCRIPT_EXPR_MAP,
    SCRIPT_EXPR_MAP_HAS,
    SCRIPT_EXPR_MAP_REMOVE,
    SCRIPT_EXPR_STRUCT,
    SCRIPT_EXPR_NIL,
    SCRIPT_EXPR_FALLBACK,
    SCRIPT_EXPR_YIELD,
    SCRIPT_EXPR_JOIN,   /* await of a task handle in left */
    SCRIPT_EXPR_CANCEL, /* cancel of a task handle in left */
    SCRIPT_EXPR_RELATION, /* Streaming relationship targets; only a foreach source. */
    SCRIPT_EXPR_UNION,
    SCRIPT_EXPR_IS,
    SCRIPT_EXPR_AS,
    SCRIPT_EXPR_UNWRAP,
    SCRIPT_EXPR_ADDRESS,
    SCRIPT_EXPR_DEREFERENCE,
    SCRIPT_EXPR_SEQUENCE,
    SCRIPT_EXPR_ARRAY_MUTATE,
    SCRIPT_EXPR_QUERY,
    SCRIPT_EXPR_QUERY_FIELD,
} script_expression_kind;
typedef enum
{
    SCRIPT_SEQUENCE_SLICE,
    SCRIPT_SEQUENCE_VIEW,
    SCRIPT_SEQUENCE_CLONE,
    SCRIPT_SEQUENCE_LENGTH,
} script_sequence_operation;
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
    SCRIPT_OP_COALESCE,
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
    /* Stable result projections cannot re-prove presence after a mutation
     * or an intervening unknown effect invalidates their original read. */
    unsigned presence_mutations, presence_epoch;
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
    bool task; /* An unawaited call to a waiting function starts a task. */
    script_type tested; /* Pattern target / safe extraction payload type. */
    uint64_t tag; /* Union case identity; zero means a statically false test. */
    bool negated;
    uint64_t *pattern_tags;
    int pattern_tag_count;
    behavior_cast_mode conversion; /* SCRIPT_EXPR_CAST range policy. */
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
    SCRIPT_STMT_REDUCE,
} script_statement_kind;
typedef enum
{
    SCRIPT_REDUCTION_ADD,
    SCRIPT_REDUCTION_MIN,
    SCRIPT_REDUCTION_MAX,
    SCRIPT_REDUCTION_ADD_UNORDERED,
} script_reduction_op;
typedef struct
{
    script_statement_kind kind;
    script_range range;
    int scope, symbol, expression, second, third, body, alternative;
    int key_symbol; /* Map iterator key binding; -1 for a value-only iterator. */
    int *children;
    int child_count;
    script_operator op;
    script_reduction_op reduction;
    unsigned effects;
    bool reverse;   /* `for <` counts down a range or an array. */
    bool exclusive; /* A `..<` range stops before its last bound. */
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
    int parameter_count, result_count;
    int depth_limit;
    uint64_t event, action;
    char *entry;
    script_range range;
    unsigned effects;
    bool pure, disabled;
    bool inferred; /* No `->`: its returns give the results. */
    bool test;     /* Marked `@test`: chibi test runs it. */
    bool fallible; /* Derived: final output contains a direct @error alternative. */
} script_function;
typedef enum
{
    SCRIPT_QUERY_HAS,
    SCRIPT_QUERY_ALL,
    SCRIPT_QUERY_ANY,
    SCRIPT_QUERY_NOT,
    SCRIPT_QUERY_RELATION, /* At least one relationship target; one row per root. */
    SCRIPT_QUERY_GENERATOR,
    SCRIPT_QUERY_VALUE,
    SCRIPT_QUERY_CHANGED,
} script_query_kind;
typedef struct
{
    script_query_kind kind;
    int description; /* One-based ordered query that owns the term. */
    bool required; /* Membership introduced by a required component binding. */
    uint64_t component;
    uint64_t source_relationship;
    int source_traversal;
    int source_variable, target_variable;
    int value; /* Boolean expression for SCRIPT_QUERY_VALUE. */
    int *children;
    int child_count;
    script_range range;
} script_query;
typedef struct
{
    int symbol;     /* -1 for a guard. */
    int expression; /* Initializer, or query term for a guard. */
    script_range range;
} script_query_stage;
typedef struct
{
    int query, query_variable_count;
    int *symbols, *captures;
    script_query_stage *stages;
    script_type row_type;
    script_range range;
    bool system;
} script_query_description;
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
/* Package inputs borrow syntax only during analysis. Keys are explicit stable
 * document identities; paths are display locations, not declaration identities. */
enum
{
    SCRIPT_PACKAGE_MAX_DOCUMENTS = 256
};
typedef struct
{
    const char *path, *key;
    const script_syntax *syntax;
} script_package_document;
typedef enum
{
    SCRIPT_PACKAGE_FUNCTION,
    SCRIPT_PACKAGE_TYPE,
    SCRIPT_PACKAGE_CONSTANT,
} script_package_export_kind;
typedef struct
{
    const char *name;
    uint64_t id;
    script_package_export_kind kind;
    script_type type; /* Constant's exact semantic type, including literal facts. */
    pin_value value; /* Constant payload; other kinds use metadata by ID. */
    const char *unit;
} script_package_export;
typedef struct
{
    const char *identity;
    const script_catalog *metadata; /* Complete by-ID signature/layout closure. */
    const script_package_export *members;
    int member_count; /* At most 4096 unique identifier names. */
    /* Declarations without pub, named so a use can say why it fails. */
    const char *const *private_names;
    int private_count;
} script_package_exports;
/* Whether exports' package declares member without pub. */
bool script_package_private(const script_package_exports *, const char *member);
#define SCRIPT_PRIVATE_MEMBER                                                  \
    "'%s' is private: declare it pub to use it from another package"
typedef struct
{
    int document;
    script_range range; /* Import start through path token end; terminator excluded. */
    const char *alias, *identity;
    /* One immutable exports view per dependency identity in this analysis.
     * Repeated imports use the same pointer; prepared owner generations have
     * their own independent lifetime and invalidation contract.
     * NULL only for self or blank imports. */
    const script_package_exports *exports;
} script_package_import;
typedef struct
{
    const script_package_document *documents;
    int document_count;
    const char *identity;
    const script_catalog *native; /* The only ambient source-visible names. */
    const script_package_import *imports;
    int import_count;
} script_package_options;

typedef struct
{
    char *path, *key, *text;
} script_source_document;

typedef struct
{
    uint64_t id, template_id;
    const char *owner;
    const script_compile_binding *bindings;
    int binding_count;
    script_range origin;
    bool evaluate;
} script_specialization_request;
/* The resolved program owns text, names, constants, origins and typed records.
 * Indices refer only to this immutable program, never to syntax/graph objects.
 * A failed analysis retains facts and diagnostics but is not executable. */
typedef struct
{
    char *text, *name, *identity;
    /* Package ranges stay local to their original document. Indices follow the
     * input array, never declaration/member sorting. Legacy analyses have none;
     * package text retains only the first document for legacy field ownership. */
    script_source_document *documents;
    int document_count;
    int package_scope; /* Shared package declaration scope; -1 for legacy roots. */
    arena constants;
    script_program_kind kind;
    script_symbol *symbols;
    script_use *uses;
    script_scope *scopes;
    script_expression *expressions;
    script_statement *statements;
    script_function *functions;
    script_query *queries;
    script_query_description *descriptions;
    int description_count, system_description;
    script_binding *bindings;
    script_catalog_type *types; /* Authored and structural union definitions. */
    int type_count;
    script_catalog_template *templates;
    int template_count;
    script_specialization_request *requests;
    int request_count;
    script_catalog_specialization *specializations;
    int specialization_count;
    script_diagnostic *diagnostics;
    script_diagnostic *warnings; /* Advisory diagnostics; never invalidate. */
    int symbol_count, use_count, scope_count, expression_count, statement_count,
        function_count, query_count, binding_count, diagnostic_count, warning_count;
    int query;
    int query_variable_count;
    int owner_symbol;
    uint64_t owner_type;
    bool valid;
} script_analysis;

script_analysis *script_analyze(const script_syntax *, const script_catalog *);
/* A stable document key is independent of its display path and declared name.
 * Project callers use a project-relative asset key for reproducible identities. */
script_analysis *script_analyze_document(const script_syntax *, const script_catalog *, const char *key);
/* Materializes requested specializations against this document's original
 * syntax/text. The analysis owns all published descriptors and requests. */
script_analysis *script_analyze_document_requested(
    const script_syntax *,
    const script_catalog *,
    const char *key,
    const script_specialization_request *,
    int request_count
);
/* Signature discovery does not resolve bodies; useful for project catalogs. */
script_analysis *script_declarations(const script_syntax *, const script_catalog *);
/* Import normalization only: obsolete native accessors may be analyzed but
 * must be rewritten and pass ordinary analysis before publication. */
script_analysis *script_analyze_migration_document(
    const script_syntax *,
    const script_catalog *,
    const char *identity,
    const script_specialization_request *,
    int count,
    bool declarations
);
script_analysis *script_declarations_document(
    const script_syntax *,
    const script_catalog *,
    const char *key
);
/* Genuine sibling-package analysis; all declarations are collected before
 * bodies. identity is a stable package key. Inputs and trees may retire after
 * return; output documents own exact paths/keys/text. The seam accepts
 * functions, constants, records and unions. Imports require explicit checked
 * document-scoped bindings; no dependency names become ambient.
 * count is bounded by SCRIPT_PACKAGE_MAX_DOCUMENTS; empty packages are invalid.
 * Export identities are independent of input order and unrelated siblings;
 * debug record ordinals and document-local byte ranges are not durable IDs. */
script_analysis *script_analyze_package(const script_package_options *);
script_analysis *script_declarations_package(const script_package_options *);
const script_source_document *script_analysis_document(const script_analysis *, int document);
/* Bounded admissibility proof only: no evaluation, host calls or mutation. */
bool script_analysis_constant(const script_analysis *, int expression);
void script_analysis_free(script_analysis *);
/* Legacy offset-only navigation selects document zero. */
const script_symbol *script_symbol_at(const script_analysis *, int offset);
const script_use *script_use_at(const script_analysis *, int offset);
#endif
