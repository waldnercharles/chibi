#include "render/material.h"

#include <assert.h>
#include <math.h>

static void unpack(unsigned color, float out[4])
{
    for (int i = 0; i < 4; i++) {
        out[i] = ((color >> (24 - i * 8)) & 255u) / 255.0f;
    }
}

static void emission(unsigned color, float intensity, float out[4])
{
    if (intensity <= 0) {
        return;
    }
    unpack(color, out);
    for (int i = 0; i < 3; i++) {
        out[i] *= intensity;
    }
}

static void absorption(unsigned filter, float alpha, float coefficient, float out[4])
{
    float channels[4];
    unpack(filter, channels);
    float amount = alpha * coefficient;
    for (int i = 0; i < 3; i++) {
        out[i] = amount * (1 - channels[i]);
    }
    out[3] = amount > 0 ? 1 : 0;
}

bool game_material_extract(const GameMaterial *material, game_material_coefficients *out)
{
    assert(material && out);
    float intensity = material->emission_intensity * material->emission_multiplier;
    if (!isfinite(material->emission_intensity) ||
        !isfinite(material->emission_multiplier) ||
        !isfinite(material->absorption) || !isfinite(intensity)) {
        return false;
    }
    game_material_coefficients result = { 0 };
    unpack(material->scene, result.scene);
    unpack(material->albedo, result.albedo);
    unpack(material->border_scene, result.border_scene);
    unpack(material->border_albedo, result.border_albedo);
    emission(material->emission_color, intensity, result.emission);
    emission(material->border_emission_color, intensity, result.border_emission);
    absorption(material->filter, result.scene[3], material->absorption, result.absorption);
    absorption(
        material->border_filter,
        result.border_scene[3],
        material->absorption,
        result.border_absorption
    );
    *out = result;
    return true;
}
