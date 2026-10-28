#ifndef CHIBI_LANGUAGE_SEMANTIC_INTERNAL_H
#define CHIBI_LANGUAGE_SEMANTIC_INTERNAL_H

#include "language/constant_proof.h"
#include "language/named_binding.h"
#include "language/signature_reader.h"
#include "types/text.h"
#include "types/type.h"

/* Optional grammar fields have a null TSNode; Tree-sitter count/range APIs
 * require a real node even when the language permits the field to be absent. */
static inline uint32_t script_analyzer_named_count(TSNode node)
{
    return ts_node_is_null(node) ? 0 : ts_node_named_child_count(node);
}
static inline uint32_t script_analyzer_child_count(TSNode node)
{
    return ts_node_is_null(node) ? 0 : ts_node_child_count(node);
}
typedef struct
{
    int symbol;
    TSNode value;
} initializer;
typedef enum
{
    BODY_PENDING,
    BODY_ACTIVE,
    BODY_DONE,
} body_state;
typedef struct
{
    int function;
    TSNode node;
    body_state state;
} function_body;
typedef struct
{
    int symbol;
    bool present;
    script_type type;
    bool typed;
    unsigned mutations;
} narrowing;
typedef script_compile_value compile_value;
typedef script_compile_binding compile_binding;
typedef struct
{
    TSNode node;
    int symbol, scope;
    bool record, type_result;
} generic_template;
typedef struct
{
    int template, function;
    uint64_t nominal;
    compile_binding *bindings;
    int *constant_arguments;
} generic_instance;
typedef struct
{
    const script_syntax *syntax;
    const script_package_document *documents;
    int document_count, package_scope;
    const script_catalog *catalog, *native;
    const script_package_import *imports;
    int import_count;
    bool *import_used;
    script_catalog catalog_view;
    script_catalog_type *catalog_types;
    script_catalog_capability *catalog_capabilities;
    script_analysis *out;
    initializer *initializers;
    function_body *bodies;
    narrowing *narrowed;
    constant_proof *constant_proofs;
    unsigned constant_generation;
    generic_template *templates;
    generic_instance *instances;
    bool instantiating;
    int compile_depth;
    int compile_expression_depth;
    int compile_statement_depth;
    bool compile_failed;
    pin_type compile_numeric, compile_result;
    int *compile_readonly;
    uint64_t specialization;
    const script_specialization_request *requested;
    int requested_count;
    int pending_specializations;
    TSNode awaited; /* The call an enclosing await applies to. */
    TSNode changing; /* The array method call a statement makes, which may change its array. */
    int scope, function, loops, depth;
    bool declarations_only, migration_import;
    bool query; /* Analyzing a system's `where`, which reports its own reads. */
} analyzer;

script_named_facts script_analyzer_named_facts(const analyzer *);
script_range script_analyzer_range(const analyzer *, TSNode);
const script_syntax *script_analyzer_syntax(const analyzer *, TSNode);
const script_syntax *script_analyzer_document_syntax(const analyzer *, int document);
bool script_analyzer_package_metadata(analyzer *);
bool script_analyzer_package_import(analyzer *, TSNode);
void script_analyzer_package_imports_check(analyzer *, bool declarations);
int script_analyzer_find_type(analyzer *, TSNode, const char *);
int script_analyzer_find_capability(analyzer *, TSNode, const char *, script_capability_kind);
bool script_analyzer_native_capability(const analyzer *, uint64_t);
int script_analyzer_package_function(analyzer *, TSNode, const char *);
int script_analyzer_package_constant(analyzer *, TSNode);
bool script_analyzer_import_namespace(analyzer *, TSNode, const char *);
/* Whether a qualified name names a declaration its package keeps private. */
bool script_analyzer_private_member(const analyzer *, TSNode, const char *name);
bool script_analyzer_generic_declare(analyzer *, TSNode);
void script_analyzer_generic_prepare(analyzer *, TSNode);
int script_analyzer_generic_call(analyzer *, TSNode, int symbol, int **arguments);
bool script_analyzer_generic_alias(analyzer *, TSNode);
script_type script_analyzer_generic_type(analyzer *, TSNode);
bool script_analyzer_compile_expression(analyzer *, TSNode, compile_binding *, compile_value *, int *budget);
bool script_analyzer_compile_call(analyzer *, TSNode, compile_binding *, compile_value *, int *budget);
bool script_analyzer_compile_as(analyzer *, TSNode, compile_binding *, pin_type, compile_value *, int *);
bool script_analyzer_generic_apply(analyzer *, TSNode, compile_binding *, compile_value *, int *budget);
bool script_analyzer_generic_member(analyzer *, script_type, const char *, compile_value *);
void script_analyzer_generic_free(analyzer *);
void script_analyzer_generic_materialize(analyzer *);
bool script_analyzer_record_field(analyzer *, TSNode, script_type, const char *, type_field *);
void script_analyzer_generic_types(analyzer *, TSNode, script_signature_generic **);
void script_analyzer_generic_export(analyzer *, TSNode, int symbol);
bool script_analyzer_generic_import_call(
    analyzer *,
    TSNode,
    const char *,
    int *expression,
    const script_catalog_capability **capability,
    int **arguments
);
bool script_analyzer_generic_import_compile(analyzer *, TSNode, compile_binding *, compile_value *, int *);
bool script_analyzer_compile_procedure(
    analyzer *,
    TSNode,
    int scope,
    compile_binding *,
    compile_value *,
    int *
);

bool script_analyzer_synchronous_await(const analyzer *, TSNode);
bool script_analyzer_readonly_callable(const analyzer *, int);

script_syntax *script_analyzer_recovery_syntax(const script_syntax *);
void script_analyzer_recovery_restore(script_analysis *, const script_syntax *);

TSNode script_analyzer_field(TSNode node, const char *name);
bool script_analyzer_is(TSNode node, const char *name);
bool script_analyzer_text_is(const analyzer *a, TSNode node, const char *text);
char *script_analyzer_text_copy(const analyzer *a, TSNode node);
void script_analyzer_diagnostic(analyzer *a, TSNode node, const char *format, ...);
char *script_analyzer_name_copy(analyzer *a, TSNode node);
pin_value script_analyzer_string_value(analyzer *, TSNode);
void script_analyzer_c_string_argument(analyzer *, TSNode, int expression, const char *boundary);
/* The one Unicode scalar a character literal holds; false after diagnosing
 * a malformed, empty or longer literal. */
bool script_analyzer_character_value(analyzer *a, TSNode node, int *value);
int script_analyzer_scope_new(analyzer *a, TSNode node);
int script_analyzer_lookup_name(const analyzer *a, const char *name);
int script_analyzer_lookup_storage(analyzer *, TSNode, script_symbol_kind);
int script_analyzer_lookup(analyzer *a, TSNode node);
int script_analyzer_symbol_new(analyzer *a, TSNode node, script_symbol_kind kind, script_type type, bool writable);
int script_analyzer_result_new(analyzer *a, TSNode node, const char *name, script_type type);
void script_analyzer_use(analyzer *a, TSNode node, int symbol, script_use_kind kind, uint64_t member);
bool script_analyzer_present(const analyzer *a, int symbol);
script_type script_analyzer_bad_type();
script_type script_analyzer_boolean_type();
void script_analyzer_require(analyzer *a, TSNode node, script_type expected, script_type actual);
const script_expression *script_analyzer_result_source(const analyzer *, int expression);
void script_analyzer_require_values(analyzer *, TSNode, int target, int value);
script_type script_analyzer_type_text(analyzer *a, TSNode node, const char *source);
script_type script_analyzer_read_type(analyzer *, TSNode, bool signature, char **unit);
script_type script_analyzer_type_node(analyzer *a, TSNode node);
/* The type a conversion callee names, or an error type with nothing recorded. */
script_type script_analyzer_conversion_type(analyzer *a, TSNode node, const char *name);
/* The type a wrap conversion reads its operand as. */
script_type script_analyzer_conversion_operand(const analyzer *, TSNode value);
script_type script_analyzer_pointer_type(analyzer *, script_type);
void script_analyzer_invalidate_pointers(analyzer *);
int script_analyzer_dereference(analyzer *, TSNode, int);
int script_analyzer_scoped_argument(analyzer *, TSNode value, script_type);
script_type script_analyzer_collection_type(analyzer *, pin_container, script_type key, script_type value);
script_type script_analyzer_signature_type(analyzer *, TSNode, char **unit);
void script_analyzer_signature_unit(analyzer *, TSNode, const char *expected, const char *actual);
int script_analyzer_expr_add(analyzer *a, script_expression expression);
int script_analyzer_sequence(analyzer *, TSNode node, TSNode object, TSNode range, script_sequence_operation);
script_type script_analyzer_expr_type(const analyzer *a, int expression);
unsigned script_analyzer_expr_effects(const analyzer *a, int expression);
int script_analyzer_initializer_find(const analyzer *a, int symbol);
void script_analyzer_resolve_initializer(analyzer *a, int symbol);
script_operator script_analyzer_operator_node(analyzer *a, TSNode node);
char *script_analyzer_qualified_name(analyzer *a, TSNode node);
int script_analyzer_callable_symbol(const analyzer *a, const char *name);
script_type script_analyzer_presence(analyzer *a, int value);
int script_analyzer_parameter_index(
    const script_catalog_parameter *parameters,
    int count,
    const char *name
);
int script_analyzer_authored_parameter_index(
    const analyzer *a,
    const script_function *function,
    const char *name
);
int script_analyzer_reference_argument(analyzer *a, TSNode node, const script_catalog_parameter *parameter);
TSNode script_analyzer_call_argument(TSNode argument);
int script_analyzer_symbol_reference(analyzer *, TSNode, script_type);
int script_analyzer_member_expression(analyzer *a, TSNode node, script_type expected);
int script_analyzer_map_operation(analyzer *, TSNode, script_expression_kind, int map, TSNode key);
bool script_analyzer_map_method(analyzer *, TSNode function, const char *name);
/* The array function method `member` calls on an array, or NULL. */
const script_catalog_capability *script_analyzer_array_method(analyzer *, TSNode member);
/* Whether an array function returns its array changed, so its method changes
 * the array in place, as `append` does and `slice` does not. */
bool script_analyzer_array_changer(const script_catalog_capability *);
int script_analyzer_call_expression(analyzer *a, TSNode node, script_type expected);
script_type script_analyzer_char_operand(script_type operand, script_type other);
bool script_analyzer_integers_match(script_type, script_type);
void script_analyzer_mismatched_integers(analyzer *, TSNode, script_type, script_type);
script_type script_analyzer_arithmetic_type(script_type left, script_type right);
int script_analyzer_expression_inner(analyzer *a, TSNode node, script_type expected);
int script_analyzer_expression(analyzer *a, TSNode node, script_type expected);
script_expression script_analyzer_number_literal(analyzer *, TSNode, script_type expected, bool negate);
/* Retypes an untyped numeric expression at `type`, reparsing its literals. */
void script_analyzer_contextual_numeric(analyzer *, int expression, script_type type);
int script_analyzer_statement_add(analyzer *a, script_statement statement);
void script_analyzer_union_results(analyzer *a, int function, TSNode body);
void script_analyzer_narrow_condition(analyzer *a, int expression_index, bool truth);
void script_analyzer_condition_type(analyzer *a, TSNode node, int value);
bool script_analyzer_function_waits(const analyzer *a, int function);
int script_analyzer_statement_block(analyzer *a, TSNode node);
int script_analyzer_statement_inner(analyzer *a, TSNode node);
int script_analyzer_statement(analyzer *a, TSNode node);
bool script_analyzer_token_present(const analyzer *a, TSNode node, const char *word);
void script_analyzer_declare_value(analyzer *a, TSNode node);
void script_analyzer_declare_constant(analyzer *a, TSNode node);
int script_analyzer_function_new(analyzer *a, TSNode node, script_function_kind kind, int symbol);
void script_analyzer_declare_function(analyzer *a, TSNode node);
int script_analyzer_query_expression(analyzer *a, TSNode node);
int script_analyzer_query_component(analyzer *a, TSNode node, uint64_t component, bool excluded);
void script_analyzer_query_intersect(analyzer *a, int term);
void script_analyzer_system_parameters(analyzer *a, TSNode root);
void script_analyzer_declaration_pass(analyzer *a, TSNode body);
void script_analyzer_value_declarations(analyzer *a, TSNode body);
int script_analyzer_scope_at(const analyzer *a, TSNode body);
void script_analyzer_handler(analyzer *a, TSNode node);
/* Binds the payload fields `list` names, by name, as parameters of function
 * in payload order; unlisted fields bind unnamed parameters. */
void script_analyzer_named_payload(
    analyzer *a,
    int function,
    TSNode list,
    const script_catalog_parameter *payload,
    int count,
    const char *event
);
uint64_t script_analyzer_expression_path(const script_analysis *out, int index);
void script_analyzer_binding_reads(analyzer *a, script_binding *binding, int index, int depth);
void script_analyzer_binding(analyzer *a, TSNode node);
bool script_analyzer_declaration_kind(TSNode node);
void script_analyzer_definition_pass(analyzer *a, TSNode body);
void script_analyzer_task_hints(analyzer *a, TSNode node);
void script_analyzer_infer_task(analyzer *a, int symbol, script_type value);
unsigned script_analyzer_statement_effects(script_analysis *out, script_statement *statement);
void script_analyzer_finish_effects(analyzer *a);
uint64_t script_analyzer_component_target(const script_analysis *, int);
void script_analyzer_finish_boxes(analyzer *a);
bool script_analyzer_binding_depends(const script_binding *reader, const script_binding *writer);
void script_analyzer_finish_bindings(analyzer *a);
void script_analyzer_callable_definition(analyzer *a, int index, TSNode body);
void script_analyzer_callable_definitions(analyzer *a, bool inferred_only);
void script_analyzer_function_type(analyzer *a, int function);
void script_analyzer_host_declarations(analyzer *a, TSNode root);
bool script_analyzer_resolve_operator(analyzer *, TSNode, TSNode, script_expression *);
bool script_analyzer_numeric_aggregate(const script_catalog *, script_type, int);
void script_analyzer_units(analyzer *a);
void script_analyzer_publish_type(analyzer *, script_catalog_type);
void script_analyzer_replace_type(analyzer *, script_catalog_type);
void script_analyzer_record_types(analyzer *, TSNode);
void script_analyzer_record_fields(analyzer *, TSNode);
bool script_analyzer_compile_time_value(analyzer *, int, int *steps, bool scalar);
const char *script_analyzer_scalar_constant(analyzer *, int, pin_type, pin_value *);
void script_analyzer_check_constants(analyzer *);
void script_analysis_fold_constants(script_analysis *);
void script_analyzer_union_declarations(analyzer *, TSNode);
void script_analyzer_union_types(analyzer *, TSNode);
void script_analyzer_union_fields(analyzer *, TSNode);
script_type script_analyzer_union_type(analyzer *, TSNode, const script_type *, int);
script_type script_analyzer_await_type(analyzer *, TSNode, script_type);
script_type script_analyzer_task_type(analyzer *, script_type, uint64_t);
int script_analyzer_union_constructor(analyzer *, TSNode);
int script_analyzer_pattern_expression(analyzer *, TSNode);
int script_analyzer_pattern(analyzer *, TSNode, int);
script_type script_analyzer_narrowed_type(const analyzer *, int, script_type);
void script_analyzer_invalidate_narrowing(analyzer *, int);
#endif
