#include "render/material.h"

#include "support/array.h"

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
        !isfinite(material->emission_multiplier) || !isfinite(material->absorption) ||
        !isfinite(intensity) || !isfinite(material->edge_light) ||
        material->edge_light < 0 || material->edge_light > 8) {
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

static bool border_cell(material_occupancy_view mask, int reach, int x, int y)
{
    if (!mask.cells[y * mask.width + x]) {
        return false;
    }
    for (int d = 1; d <= reach; d++) {
        if (x < d || y < d || x + d >= mask.width || y + d >= mask.height ||
            !mask.cells[y * mask.width + x - d] ||
            !mask.cells[y * mask.width + x + d] ||
            !mask.cells[(y - d) * mask.width + x] ||
            !mask.cells[(y + d) * mask.width + x]) {
            return true;
        }
    }
    return false;
}

void render_material_border_build(render_material_border *border, material_occupancy_view mask, int reach)
{
    assert(border && mask.cells && mask.width > 0 && mask.height > 0 && reach > 0);
    aclear(border->rects);
    asetlen(border->row, mask.width);
    for (int x = 0; x < mask.width; x++) {
        border->row[x] = -1;
    }
    for (int y = 0; y < mask.height; y++) {
        for (int x = 0; x < mask.width; x++) {
            if (!border_cell(mask, reach, x, y)) {
                continue;
            }
            int width = 1;
            while (x + width < mask.width && border_cell(mask, reach, x + width, y)) {
                width++;
            }
            int index = border->row[x];
            if (index >= 0 && border->rects[index].width == width &&
                border->rects[index].y + border->rects[index].height == y) {
                border->rects[index].height++;
            } else {
                border->row[x] = acount(border->rects);
                apush(border->rects, ((render_material_border_rect){ x, y, width, 1 }));
            }
            x += width - 1;
        }
    }
}

void render_material_border_free(render_material_border *border)
{
    afree(border->rects);
    afree(border->row);
    *border = (render_material_border){ 0 };
}
