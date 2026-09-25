#ifndef CHIBI_REFLECT_INTERNAL_H
#define CHIBI_REFLECT_INTERNAL_H

#include "graph/model/pin.h"
#include "support/array.h"

#include <clang-c/Index.h>
#include <stdint.h>
#include <stdio.h>

typedef struct
{
    char *name;       /* field name */
    char *spelling;   /* C type spelling, e.g. "float", "Vec3" */
    char *handle;     /* Nominal handle type when explicitly annotated. */
    const char *pin;  /* pin_type enum name, e.g. "PIN_F32" */
    long long offset; /* bytes */
    long long size;   /* bytes */
} field_info;

static inline const char *field_type_name(const field_info *field)
{
    return field->handle ? field->handle : field->spelling;
}

typedef struct
{
    char *name;
    char *file; /* header basename where the definition lives */
    unsigned line;
    field_info *fields;
    long long size;
    int component;
    int dense;
    char *alias_of;
} struct_info;

typedef struct
{
    char *name;
    char *label;
    int value;
} enum_value_info;

typedef struct
{
    char *name;
    enum_value_info *values;
} enum_info;

typedef struct
{
    char *name;
    char *ret;           /* return type spelling */
    const char *ret_pin; /* pin_type enum name for the return, or NULL for void */
    int read; /* contextual pure read returning const char * on error */
    int exec;
    int ret_void;
    char **pname;         /* param names */
    char **plabel;        /* optional display names for pins */
    char **ptype;         /* param type spellings */
    const char **ppin;    /* pin_type enum names per param */
    int *pout;            /* explicit NODE_OUT pointer parameters */
    int *pservice;        /* service-backed parameters */
    char **pservice_type; /* nominal service type */
    char **pout_type;     /* pointee spelling for explicit outputs */
    char **reads;         /* NODE_READS type names */
    char **writes;        /* NODE_WRITES type names */
    unsigned effect_flags;
    char *display_name;
    char *category;
    char *keywords;
    char *jit_kind;
    unsigned jit_flags;
    char *conversion_from;
    char *conversion_to;
} fn_info;

typedef struct
{
    struct_info *structs;
    fn_info *fns;
    enum_info *enums;
} collect_ctx;

extern int g_failed;
extern const char *g_prefix;
extern const collect_ctx *g_collection;

FILE *begin_generated(const char *path, char **temporary);
void publish_generated(FILE *file, char *temporary, const char *path);
int ends_with(const char *s, const char *suffix);
uint64_t stable_service_id(const char *name);
uint64_t component_id(const struct_info *s);
const char *component_key(const struct_info *s);
void load_component_manifest(const char *path);
void record_new_components(const collect_ctx *ctx);
void write_component_manifest();
void dispose_component_manifest();
uint64_t hash_bytes(uint64_t hash, const void *data, size_t size);
uint64_t hash_string(uint64_t hash, const char *text);
const char *pin_member(const char *pin);
void emit_pin_spec(FILE *out, const char *name, const char *pin, const char *spelling);
void emit_fn(FILE *out, const fn_info *f);
void emit_read_fn(FILE *out, const fn_info *f);
void emit_struct_nodes(FILE *out, const struct_info *s);
void emit_exec_fn(FILE *out, const fn_info *f);
void emit_exec_node(FILE *out, const fn_info *f);
void emit_fn_node(FILE *out, const fn_info *f);
void emit_read_node(FILE *out, const fn_info *f);
bool component_unwrap(const struct_info *s);
void emit_component_unwrap(FILE *out, const struct_info *s);
void emit_component_nodes(FILE *out, const struct_info *s);
void emit_component_entity_nodes(FILE *out, const struct_info *s);
bool component_field_settable(const field_info *field);
void emit_component_field_nodes(FILE *out, const struct_info *s);
void emit_component_entity_nodes_desc(FILE *out, const struct_info *s);
void emit_component_nodes_desc(FILE *out, const struct_info *s);
void emit_component_accessors(FILE *out, const struct_info *s);
void emit(FILE *out, const collect_ctx *ctx);

void reflect_collect(CXTranslationUnit tu, collect_ctx *ctx);
void reflect_dispose(collect_ctx *ctx);

#endif
