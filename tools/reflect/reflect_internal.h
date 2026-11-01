#ifndef CHIBI_REFLECT_INTERNAL_H
#define CHIBI_REFLECT_INTERNAL_H

#include "support/array.h"
#include "types/value.h"

#include <clang-c/Index.h>
#include <stdint.h>
#include <stdio.h>

typedef struct
{
    char *name;        /* field name */
    char *spelling;    /* C type spelling, e.g. "float", "Vec3" */
    char *handle;      /* Nominal handle type when explicitly annotated. */
    char *description; /* First paragraph of the field's comment, or NULL. */
    const char *pin;   /* pin_type enum name, e.g. "PIN_F32" */
    long long offset;  /* bytes */
    long long size;    /* bytes */
    bool color;        /* Explicit packed RGBA semantic. */
    char *unit;        /* Numeric presentation unit, or NULL. */
    double min, max;   /* Numeric presentation range; equal when unbounded. */
    bool range_contract;
    double range_default;
} field_info;

/* The authored name of the type C spells `spelling`. */
const char *authored_type(const char *spelling);
static inline const char *field_type_name(const field_info *field)
{
    return field->handle ? field->handle : authored_type(field->spelling);
}

typedef struct
{
    char *name;
    char *c_name; /* C spelling when it differs from the authored name. */
    char *file;   /* header basename where the definition lives */
    unsigned line;
    field_info *fields;
    long long size;
    int component;
    int dense;
    char *alias_of;
    char *constructor_name;
    char *derived_from; /* The source component a derived component follows. */
} struct_info;

static inline const char *struct_c_name(const struct_info *s)
{
    return s->c_name ? s->c_name : s->name;
}

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
    unsigned effect_flags;
    char *display_name;
    char *category;
    char *keywords;
    char *description;
    char *jit_kind;
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
const char *symbol_prefix();
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
void emit_string(FILE *, const char *);
#define REFLECT_DESC_MAKE "Builds a %s from its fields."
#define REFLECT_DESC_BREAK "Splits a %s into its fields."
#define REFLECT_DESC_UNWRAP "Returns the %s a %s wraps."
#define REFLECT_DESC_FIELD "Writes %s's %s on Target, which defaults to Self."
#define REFLECT_DESC_GET_ENTITY "Reads %s from Entity."
#define REFLECT_DESC_HAS_ENTITY "Whether Entity is alive and has %s."
#define REFLECT_DESC_SET_ENTITY "Writes %s on Entity."
#define REFLECT_DESC_GET_SELF "Reads %s from Self."
#define REFLECT_DESC_SET_SELF "Writes %s on Self."
#define REFLECT_DESC_HAS_SELF "Whether Self has %s."
#define REFLECT_DESC_ADD                                                       \
    "Adds %s with default values to Self; Value is whether it was added."
#define REFLECT_DESC_REMOVE                                                    \
    "Removes %s from Self; Value is whether it was there."
void emit_function_presentation(FILE *, const fn_info *);
void emit_struct_presentation(
    FILE *,
    const struct_info *,
    const field_info *,
    const char *format,
    const char *category,
    bool field
);
bool component_unwrap(const struct_info *s);
void emit_execution_unwrap(FILE *out, const struct_info *s);
bool component_field_settable(const field_info *field);
const struct_info *derived_source(const struct_info *s);
void emit_runtime(FILE *out, const collect_ctx *ctx);
void emit_types(FILE *out, const collect_ctx *ctx);
void emit_native_catalog(FILE *out, const collect_ctx *ctx);
void emit_execution_host(FILE *out, const fn_info *, bool external);
void emit_execution_result(FILE *out, const char *pin, const char *expression, const char *type, int index);

void reflect_collect(CXTranslationUnit tu, collect_ctx *ctx);
void reflect_dispose(collect_ctx *ctx);

#endif
