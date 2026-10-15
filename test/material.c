#include "render/material.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.00001f);
}

void test_material()
{
    Material material = {
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
    material_coefficients result;
    assert(material_extract(&material, &result));
    near(result.scene[0], 32 / 255.0f);
    near(result.scene[3], 128 / 255.0f);
    near(result.albedo[2], 128 / 255.0f);
    near(result.albedo[3], 32 / 255.0f);
    near(result.emission[0], 8);
    near(result.emission[1], 8 * 128 / 255.0f);
    near(result.emission[3], 64 / 255.0f);
    near(result.absorption[0], 0);
    near(result.absorption[1], 128 / 255.0f * 0.75f * (1 - 128 / 255.0f));
    near(result.absorption[2], 128 / 255.0f * 0.75f);
    near(result.absorption[3], 1);
    near(result.border_scene[3], 64 / 255.0f);
    near(result.border_emission[2], 8);
    near(result.border_emission[3], 128 / 255.0f);
    near(result.border_albedo[0], 16 / 255.0f);
    near(result.border_absorption[0], 64 / 255.0f * 0.75f);
    near(result.border_absorption[1], 0);
    near(result.border_absorption[2], 64 / 255.0f * 0.75f * (1 - 128 / 255.0f));
    near(result.border_absorption[3], 1);

    material.emission_multiplier = 0;
    material.scene &= 0xFFFFFF00u;
    assert(material_extract(&material, &result));
    for (int i = 0; i < 4; i++) {
        near(result.emission[i], 0);
        near(result.border_emission[i], 0);
        near(result.absorption[i], 0);
    }
    near(result.border_absorption[3], 1);
    material.emission_multiplier = -1;
    material.absorption = 0;
    assert(material_extract(&material, &result));
    near(result.emission[3], 0);
    near(result.border_absorption[3], 0);

    material_coefficients before = result;
    material.absorption = NAN;
    assert(!material_extract(&material, &result));
    assert(memcmp(&before, &result, sizeof result) == 0);
    material.absorption = 1;
    material.emission_intensity = FLT_MAX;
    material.emission_multiplier = 2;
    assert(!material_extract(&material, &result));
    assert(memcmp(&before, &result, sizeof result) == 0);
    material.emission_multiplier = INFINITY;
    assert(!material_extract(&material, &result));
    puts("material PASS: linear coefficients, HDR emission, independent border absorption and invalid inputs");
}
