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
/* An unlit material whose base color is a black-to-white ramp of the x of
 * coordinate node `input`, such as "Shader UV". */
bool test_register_coordinate(const char *name, const char *input);
/* A lit material with the output's white base whose Normal is a Normal Map
 * of the constant linear `color` in `convention`. */
bool test_register_normal_map(const char *name, const float color[3], shader_normal_convention convention);
/* The Normal Map convention whose label Studio shows is `label`. */
shader_normal_convention test_normal_convention(const char *label);
/* A lit material with the output's white base and constant `occlusion`. */
bool test_register_occlusion(const char *name, float occlusion);
/* An unlit white material with constant `opacity` drawn by alpha `mode` with
 * `cutoff`. */
bool test_register_alpha(const char *name, shader_alpha_mode mode, float opacity, float cutoff);
/* A lit white material, masked at cutoff 0 by its object position's z, so
 * it keeps the half of a solid at z >= 0 and cuts the rest. */
bool test_register_half_mask(const char *name);
/* A lit white material, masked at cutoff 0.5 by the negated x of its View
 * Direction, so it keeps what is seen from well toward -X and cuts the rest. */
bool test_register_view_mask(const char *name);
/* An unlit masked material whose Opacity is NaN over its surface, as the
 * fraction of U scaled past the float limit is, with an Alpha Cutoff of 0.5. */
bool test_register_unknown_opacity(const char *name);
/* A lit material whose base color sums `count` images, each its own
 * content/images/test-N.png of one white pixel, and with `ramp`, a white
 * Color Ramp. */
bool test_register_images(const char *name, int count, bool ramp);
/* A lit .6 gray material for 2D relief. Its Normal leans by `tilt` sideways
 * (x) and up (z), its roughness is constant, and its Height is `height` world
 * units plus `slope` times world x. */
typedef struct
{
    float tilt[2], roughness, height, slope;
} test_relief;
bool test_register_relief(const char *name, test_relief relief);
/* An unlit material that is red where world x is at least .5 and black
 * elsewhere. */
bool test_register_world_step(const char *name);
/* An unlit material whose color is its "Tint" parameter, `tint` unless an
 * entity overrides it. */
bool test_register_tint(const char *name, const float tint[3]);
/* A shape graph of a circle of `radius` at the origin, with half extents of
 * twice its radius. */
bool test_register_circle_shape(const char *name, float radius);
/* A lit material whose base color, emission, roughness and height are its
 * "Tint" and "Glow" vector and "Roughness" and "Height" scalar parameters. */
bool test_register_parameters(const char *name);
/* A shape graph of a circle at the origin whose radius is its "Radius"
 * parameter, with half extents of `bounds`. */
bool test_register_radius_shape(const char *name, float bounds);
/* A model graph of one sphere of `radius` at the origin. */
bool test_register_model_sphere(const char *name, float radius);

#endif
