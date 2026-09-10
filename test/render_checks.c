#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/sdf_shapes.h"

#include <stdio.h>

extern int test_interior_border();
extern int test_radiance();
extern int test_arena3d();
extern int test_arena_post();
extern int test_shader_graph_gpu();
extern int test_surface_gpu();
extern int test_shader_effects_gpu();
extern int test_shader_image_gpu();
extern int test_shader_image_routes();
extern int test_shader_program_gpu();
extern int test_sdf_coverage(app_sdf_shapes shapes);

int render_checks_run(const char *program)
{
    CF_Result result = cf_make_app(
        "chibi render checks",
        0,
        0,
        0,
        256,
        256,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "render checks app: %s\n", result.details ? result.details : "unknown error");
        return 1;
    }

    app_scene_renderer_initialize();
    int failed = test_interior_border();
    failed |= test_arena3d();
    failed |= test_arena_post();
    failed |= test_shader_graph_gpu();
    failed |= test_surface_gpu();
    failed |= test_shader_effects_gpu();
    failed |= test_shader_program_gpu();
    failed |= test_shader_image_gpu();
    failed |= test_shader_image_routes();
    app_sdf_shapes shapes = app_sdf_shapes_init();
    failed |= test_sdf_coverage(shapes);
    failed |= test_radiance();
    cf_destroy_app();
    return failed;
}
