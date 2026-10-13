#ifndef CHIBI_ASSET_SCHEMA_H
#define CHIBI_ASSET_SCHEMA_H

#include "support/asset_value.h"

#include <stdbool.h>

/* Structural authoring grammar. Canonical loaders still own cross-asset,
 * numeric, version and execution constraints. Static records live forever. */
typedef enum
{
    ASSET_SCHEMA_ANY,
    ASSET_SCHEMA_STRING,
    ASSET_SCHEMA_BOOL,
    ASSET_SCHEMA_INTEGER,
    ASSET_SCHEMA_NUMBER,
    ASSET_SCHEMA_OBJECT,
    ASSET_SCHEMA_ARRAY,
} asset_schema_value_kind;

typedef struct asset_schema asset_schema;
typedef struct asset_schema_field
{
    const char *name, *description;
    asset_schema_value_kind type;
    bool required;
    const asset_schema *object;
    asset_schema_value_kind element_type;
    const asset_schema *element_object;
    const char *reference_kind;
    const char *const *choices;
    int choice_count;
} asset_schema_field;

struct asset_schema
{
    const char *kind, *folder, *extension, *description;
    const asset_schema_field *fields;
    int field_count;
    /* Complete structural field inventory; not semantic validation coverage.
     * Partial schemas deliberately allow unknown properties. */
    bool complete;
    bool executable;
};

typedef struct
{
    int start, end, line, column;
    char message[256];
    char code[48], path[384], expected[256];
} asset_schema_diagnostic;

int asset_schema_count();
const asset_schema *asset_schema_at(int index);
const asset_schema *asset_schema_find(const char *kind);
/* Recognizes project.kdl and content-family paths, not displayed asset names. */
const asset_schema *asset_schema_for_path(const char *path);
const asset_schema_field *asset_schema_field_find(const asset_schema *, const char *name);
const char *asset_schema_value_name(asset_schema_value_kind);
/* Owned support/array.h array, NULL when there are no structural problems.
 * Ranges refer to the original parsed text; missing fields point at their parent.
 * Executable source is validated by the canonical language frontend instead. */
asset_schema_diagnostic *asset_schema_validate(const asset_schema *, const av_value *, const char *text);
/* Allocates into doc. Examples use the same canonical creation serializers as
 * Studio. Graph schemas mark their incomplete nested metadata explicitly. */
av_value *asset_schema_describe(av_doc *doc, const asset_schema *, bool example);

#endif
