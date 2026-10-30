#include "host/native_default.h"

#include "runtime/arithmetic_definition.h"
#include "runtime/arrays_definition.h"
#include "runtime/comparison_definition.h"
#include "runtime/conversion_definition.h"
#include "runtime/core_definition.h"
#include "runtime/math_definition.h"
#include "runtime/selection_definition.h"
#include "runtime/sets_definition.h"
#include "runtime/text_definition.h"
#include "runtime/vectors_definition.h"
#include "support/array.h"

#include <stdio.h>
native_capabilities *host_native_standalone_catalog_new(
    const native_capability_pack *native,
    int native_count,
    const native_definition_pack *extra,
    int extra_count,
    char *error,
    size_t size
)
{
    const native_definition_pack standalone[] = {
        CORE_DEFINITION_PACK,
        NATIVE_TEXT_DEFINITION_PACK,
        NATIVE_ARRAY_DEFINITION_PACK,
        NATIVE_VECTOR_DEFINITION_PACK,
        NATIVE_MATH_DEFINITION_PACK,
        NATIVE_ARITHMETIC_DEFINITION_PACK,
        NATIVE_SELECTION_DEFINITION_PACK,
        NATIVE_SET_DEFINITION_PACK,
        NATIVE_COMPARISON_DEFINITION_PACK,
        NATIVE_CONVERSION_DEFINITION_PACK,
    };
    native_definition_pack *packs = NULL;
    for (size_t i = 0; i < sizeof standalone / sizeof *standalone; i++) {
        apush(packs, standalone[i]);
    }
    for (int i = 0; i < extra_count; i++) {
        apush(packs, extra[i]);
    }
    native_capabilities *out = native_capabilities_from_definition_packs(
        native,
        native_count,
        packs,
        acount(packs),
        error,
        size
    );
    afree(packs);
    return out;
}
