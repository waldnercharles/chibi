#include "test/support/reflected_types.h"

#include <stddef.h>

#define FIELD(T, member, pin, spelling)                                        \
    { .name = #member,                                                         \
      .type = pin,                                                             \
      .offset = offsetof(T, member),                                           \
      .size = sizeof(((T *)0)->member),                                        \
      .type_name = spelling }
static const type_field pair_fields[] = { FIELD(whole_pair, x, PIN_F32, "float"),
                                          FIELD(whole_pair, y, PIN_F32, "float") };
static const type_field record_fields[] = {
    FIELD(whole_record, tag, PIN_U8, "byte"),
    FIELD(whole_record, position, PIN_STRUCT, "WholePair"),
    FIELD(whole_record, weight, PIN_F64, "double"),
    FIELD(whole_record, active, PIN_BOOL, "bool"),
    FIELD(whole_record, stamp, PIN_I64, "int64")
};
#undef FIELD
const type_desc WHOLE_PAIR_TYPE = { .name = "WholePair",
                                    .size = sizeof(whole_pair),
                                    .fields = pair_fields,
                                    .field_count = 2 };
const type_desc WHOLE_RECORD_TYPE = { .name = "WholeRecord",
                                      .size = sizeof(whole_record),
                                      .fields = record_fields,
                                      .field_count = 5 };

static const type_field inner_fields[] = {
    { .name = "text",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(whole_managed_inner, text),
      .size = sizeof(const char *) },
    { .name = "weight",
      .type = PIN_F32,
      .type_name = "float",
      .offset = offsetof(whole_managed_inner, weight),
      .size = sizeof(float) }
};
const type_desc WHOLE_MANAGED_INNER_TYPE = { .name = "ManagedInner",
                                             .size = sizeof(whole_managed_inner),
                                             .fields = inner_fields,
                                             .field_count = 2 };
static const type_field fields[] = { { .name = "label",
                                       .type = PIN_NAME,
                                       .type_name = "name",
                                       .offset = offsetof(whole_managed, label),
                                       .size = sizeof(const char *) },
                                     { .name = "nested",
                                       .type = PIN_STRUCT,
                                       .type_name = "ManagedInner",
                                       .offset = offsetof(whole_managed, nested),
                                       .size = sizeof(whole_managed_inner) },
                                     { .name = "count",
                                       .type = PIN_I32,
                                       .type_name = "int",
                                       .offset = offsetof(whole_managed, count),
                                       .size = sizeof(int) } };
const type_desc WHOLE_MANAGED_TYPE = { .name = "ManagedRecord",
                                       .size = sizeof(whole_managed),
                                       .fields = fields,
                                       .field_count = 3 };
static const type_field alias_fields[] = {
    { .name = "x",
      .type = PIN_F32,
      .offset = offsetof(alias_vector, x),
      .size = sizeof(float),
      .type_name = "float" },
    { .name = "y",
      .type = PIN_F32,
      .offset = offsetof(alias_vector, y),
      .size = sizeof(float),
      .type_name = "float" },
};
const type_desc WHOLE_ALIAS_SOURCE_TYPE = { .name = "AliasSource",
                                            .size = sizeof(alias_vector),
                                            .fields = alias_fields,
                                            .field_count = 2 };
const type_desc WHOLE_ALIAS_TARGET_TYPE = { .name = "AliasTarget",
                                            .size = sizeof(alias_vector),
                                            .fields = alias_fields,
                                            .field_count = 2,
                                            .alias_of = "AliasSource" };

const type_desc WHOLE_SEARCH_TYPE = { .name = "SearchKind", .size = sizeof(int32_t) };
static const type_desc *const fixture_types[] = {
    &WHOLE_PAIR_TYPE,    &WHOLE_RECORD_TYPE,       &WHOLE_MANAGED_INNER_TYPE,
    &WHOLE_MANAGED_TYPE, &WHOLE_ALIAS_SOURCE_TYPE, &WHOLE_ALIAS_TARGET_TYPE,
    &WHOLE_SEARCH_TYPE
};
const type_catalog_pack TEST_REFLECTED_TYPES_PACK = {
    .types = fixture_types,
    .type_count = sizeof fixture_types / sizeof *fixture_types
};
