#include "host/native_default.h"

#include "runtime/arithmetic_definition.h"
#include "runtime/arrays_definition.h"
#include "runtime/audio_definition.h"
#include "runtime/behaviors/entities_definition.h"
#include "runtime/behaviors/scene_flow_definition.h"
#include "runtime/behaviors/tilemap_definition.h"
#include "runtime/comparison_definition.h"
#include "runtime/conversion_definition.h"
#include "runtime/core_definition.h"
#include "runtime/fields/definition.h"
#include "runtime/generated_capabilities.h"
#include "runtime/input_definition.h"
#include "runtime/materials_definition.h"
#include "runtime/math_definition.h"
#include "runtime/particles_definition.h"
#include "runtime/physics3d_definition.h"
#include "runtime/physics_definition.h"
#include "runtime/save_definition.h"
#include "runtime/selection_definition.h"
#include "runtime/sets_definition.h"
#include "runtime/state_definition.h"
#include "runtime/terrain_storage_definition.h"
#include "runtime/text_definition.h"
#include "runtime/ui_definition.h"
#include "runtime/url_definition.h"
#include "runtime/vectors_definition.h"
#include "runtime/visibility_definition.h"
#include "support/array.h"

#include <stdio.h>
#define MAX_ADDITIONAL_PACKS 64
native_capabilities *host_native_default_catalog_new(
    const native_capability_pack *native,
    int native_count,
    const native_definition_pack *definitions,
    int definition_count,
    char *error,
    size_t size
)
{
    if (native_count < 0 || definition_count < 0 ||
        native_count > MAX_ADDITIONAL_PACKS || definition_count > MAX_ADDITIONAL_PACKS ||
        (native_count && !native) || (definition_count && !definitions)) {
        if (size) {
            snprintf(error, size, "Invalid additional application packs");
        }
        return NULL;
    }
    native_definition_pack *selected = NULL;
    apush(selected, CORE_DEFINITION_PACK);
    apush(selected, ENTITY_DEFINITION_PACK);
    apush(selected, TILEMAP_DEFINITION_PACK);
    apush(selected, AUDIO_DEFINITION_PACK);
    apush(selected, FIELD_DEFINITION_PACK);
    apush(selected, NATIVE_ARRAY_DEFINITION_PACK);
    apush(selected, NATIVE_STATE_DEFINITION_PACK);
    apush(selected, NATIVE_VECTOR_DEFINITION_PACK);
    apush(selected, NATIVE_MATH_DEFINITION_PACK);
    apush(selected, NATIVE_ARITHMETIC_DEFINITION_PACK);
    apush(selected, NATIVE_SELECTION_DEFINITION_PACK);
    apush(selected, NATIVE_SET_DEFINITION_PACK);
    apush(selected, NATIVE_COMPARISON_DEFINITION_PACK);
    apush(selected, NATIVE_CONVERSION_DEFINITION_PACK);
    apush(selected, NATIVE_TEXT_DEFINITION_PACK);
    apush(selected, SAVE_DEFINITION_PACK);
    apush(selected, TERRAIN_STORAGE_DEFINITION_PACK);
    apush(selected, SCENE_DEFINITION_PACK);
    apush(selected, PARTICLES_DEFINITION_PACK);
    apush(selected, UI_DEFINITION_PACK);
    apush(selected, URL_DEFINITION_PACK);
    apush(selected, PHYSICS_DEFINITION_PACK);
    apush(selected, PHYSICS3D_DEFINITION_PACK);
    apush(selected, MATERIAL_DEFINITION_PACK);
    apush(selected, VISIBILITY_DEFINITION_PACK);
    apush(selected, INPUT_DEFINITION_PACK);
    for (int i = 0; i < definition_count; i++) {
        apush(selected, definitions[i]);
    }
    native_capability_pack *inputs = NULL;
    apush(inputs, NATIVE_PACK);
    for (int i = 0; i < native_count; i++) {
        apush(inputs, native[i]);
    }
    native_capabilities *out = host_native_catalog_new(
        inputs,
        acount(inputs),
        selected,
        acount(selected),
        error,
        size
    );
    afree(inputs);
    afree(selected);
    return out;
}
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
