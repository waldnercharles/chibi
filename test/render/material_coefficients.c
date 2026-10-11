#include "render/material.h"
#include "third_party/pico_headers/pico_unit.h"

#include <float.h>
#include <math.h>
#include <string.h>

/* The linear coefficients the material raster draws a Material with. */

static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < 0.00001f;
}

static Material lit_material()
{
    return (Material){
        .scene = 0x20408080,
        .albedo = 0x40608020,
        .filter = 0xFF800000,
        .emission_color = 0xFF804040,
        .emission_intensity = 4,
        .emission_multiplier = 2,
        .absorption = 0.75f,
        .border_scene = 0x90A0B040,
        .border_emission_color = 0x4080FF80,
        .border_albedo = 0x10203040,
        .border_filter = 0x00FF80FF,
    };
}

/* Packed colors become unpremultiplied linear RGBA, emission stays HDR, and
 * borders absorb independently of the body. */
TEST_CASE(material_coefficients_linear_and_hdr)
{
    Material material = lit_material();
    material_coefficients result;
    REQUIRE(material_extract(&material, &result));
    REQUIRE(near(result.scene[0], 32 / 255.0f));
    REQUIRE(near(result.scene[3], 128 / 255.0f));
    REQUIRE(near(result.albedo[2], 128 / 255.0f));
    REQUIRE(near(result.albedo[3], 32 / 255.0f));
    REQUIRE(near(result.emission[0], 8));
    REQUIRE(near(result.emission[1], 8 * 128 / 255.0f));
    REQUIRE(near(result.emission[3], 64 / 255.0f));
    REQUIRE(near(result.absorption[0], 0));
    REQUIRE(near(result.absorption[1], 128 / 255.0f * 0.75f * (1 - 128 / 255.0f)));
    REQUIRE(near(result.absorption[2], 128 / 255.0f * 0.75f));
    REQUIRE(near(result.absorption[3], 1));
    REQUIRE(near(result.border_scene[3], 64 / 255.0f));
    REQUIRE(near(result.border_emission[2], 8));
    REQUIRE(near(result.border_emission[3], 128 / 255.0f));
    REQUIRE(near(result.border_albedo[0], 16 / 255.0f));
    REQUIRE(near(result.border_absorption[0], 64 / 255.0f * 0.75f));
    REQUIRE(near(result.border_absorption[1], 0));
    REQUIRE(near(result.border_absorption[2], 64 / 255.0f * 0.75f * (1 - 128 / 255.0f)));
    REQUIRE(near(result.border_absorption[3], 1));
    return true;
}

/* No multiplier means no emission, a transparent scene absorbs nothing, and
 * no absorption leaves borders clear. */
TEST_CASE(material_coefficients_zeroed_terms)
{
    Material material = lit_material();
    material.emission_multiplier = 0;
    material.scene &= 0xFFFFFF00u;
    material_coefficients result;
    REQUIRE(material_extract(&material, &result));
    for (int i = 0; i < 4; i++) {
        REQUIRE(near(result.emission[i], 0));
        REQUIRE(near(result.border_emission[i], 0));
        REQUIRE(near(result.absorption[i], 0));
    }
    REQUIRE(near(result.border_absorption[3], 1));
    material.emission_multiplier = -1;
    material.absorption = 0;
    REQUIRE(material_extract(&material, &result));
    REQUIRE(near(result.emission[3], 0));
    REQUIRE(near(result.border_absorption[3], 0));
    return true;
}

/* Scalars that are not finite, or whose product overflows, are refused and
 * leave the previous coefficients untouched. */
TEST_CASE(material_coefficients_refuse_invalid_scalars)
{
    Material material = lit_material();
    material_coefficients result;
    REQUIRE(material_extract(&material, &result));
    material_coefficients before = result;
    material.absorption = NAN;
    REQUIRE(!material_extract(&material, &result));
    REQUIRE(memcmp(&before, &result, sizeof result) == 0);
    material.absorption = 1;
    material.emission_intensity = FLT_MAX;
    material.emission_multiplier = 2;
    REQUIRE(!material_extract(&material, &result));
    REQUIRE(memcmp(&before, &result, sizeof result) == 0);
    material.emission_multiplier = INFINITY;
    REQUIRE(!material_extract(&material, &result));
    return true;
}

void test_render_material_coefficients()
{
    RUN_TEST_CASE(material_coefficients_linear_and_hdr);
    RUN_TEST_CASE(material_coefficients_zeroed_terms);
    RUN_TEST_CASE(material_coefficients_refuse_invalid_scalars);
}
