#ifndef CHIBI_TEST_RENDER_MATERIAL_GRAPHS_H
#define CHIBI_TEST_RENDER_MATERIAL_GRAPHS_H

#include "runtime/shaders/program.h"

#include <stdbool.h>

/* Material graphs registered straight into the shader catalog. The tools
 * dispatcher adds the shader types to its type catalog while render cases
 * run, so graph defaults can hold colors and ramp stops. */

/* A material with the output's white base and constant roughness and
 * metallic. */
bool test_register_surface(const char *name, float roughness, float metallic);
/* An unlit material whose base color is a ramp from `from` to `to` over
 * `count` evenly spaced stops, read a quarter of the way along. */
bool test_register_ramp(const char *name, int count, const float from[4], const float to[4]);

#endif
