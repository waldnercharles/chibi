#include "render/shader_program.h"

#include "render/draw_textures.h"
#include "render/shader_fragment.h"
#include "render/shader_images.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    render_shader_set set;
    CF_Shader draw;
    CF_ComputeShader compute;
    float (*ramp_data)[4];
    int ramp_vec4_count;
    unsigned ramp_revision;
    unsigned revision;
    unsigned attempted_revision;
    unsigned checked_revision, checked_ramp_revision;
    int checked_ramp_count;
    char error[256];
} program;

struct render_programs
{
    render_program_kind kind;
    char *vertex, *body, *scene;
    program *entries;
    char error[256];
};

static unsigned shader_compile_count;

char *render_shader_source(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "/chibi-render/%s", name);
    char *source = cf_fs_read_entire_file_to_memory_and_nul_terminate(path, NULL);
    char *copy = source ? xstrdup(source) : NULL;
    cf_free(source);
    return copy;
}

render_programs *render_programs_new(render_program_kind kind)
{
    render_programs *programs = xcalloc(1, sizeof *programs);
    programs->kind = kind;
    if (kind == RENDER_PROGRAM_ARENA) {
        programs->vertex = render_shader_source("arena3d.vert");
        programs->body = render_shader_source("arena3d.frag");
        programs->scene = render_shader_source("arena3d_scene.glsl");
    } else if (kind == RENDER_PROGRAM_FRAGMENT) {
        programs->body = render_shader_fragment_source();
    } else {
        programs->body = render_shader_source("hrc/entity_raster.shd");
    }
    return programs;
}

static bool compile(render_programs *programs, program *entry)
{
    shader_compile_count++;
    bool compute = programs->kind == RENDER_PROGRAM_COMPUTE;
    int ramp_vec4_count = render_shader_ramp_vec4_count(&entry->set);
    float (*ramp_data)[4] = ramp_vec4_count
                              ? xcalloc(RENDER_SHADER_RAMP_VEC4_MAX, sizeof *ramp_data)
                              : NULL;
    if (ramp_data) {
        render_shader_ramp_payload(&entry->set, ramp_data);
    }
    char *graphs = render_shader_glsl(&entry->set, compute ? 0 : 2, compute ? 0 : 1);
    const char *scene = programs->scene ? programs->scene : "";
    const char *uniforms = programs->kind == RENDER_PROGRAM_FRAGMENT
                             ? "layout(set=3,binding=1) uniform shd_uniforms { "
                               "vec2 sg_lighting; vec4 sg_entity[85]; vec4 "
                               "sg_ramp_data[SG_RAMP_COUNT]; };\n"
                         : compute
                             ? "layout(set=2,binding=0) uniform Params { "
                               "float u_world_min_x; "
                               "float u_world_max_y; vec2 u_unit_per_px; "
                               "int u_tiles_x; int u_tiles_y; int u_canvas_w; "
                               "int u_canvas_h; "
                               "int u_first_entity; int u_last_entity; "
                               "int u_metaball_group; int u_accumulate; "
                               "vec4 sg_ramp_data[SG_RAMP_COUNT]; };\n"
                             : "";
    size_t bytes = strlen(graphs) + strlen(programs->body) + strlen(scene) +
                   strlen(uniforms) + 256;
    char *source = xmalloc(bytes);
    snprintf(
        source,
        bytes,
        "#define SG_GRAPHS 1\n#define SG_RAMP_COUNT %d\n"
        "#define SG_ENTITY_BINDING %d\n"
        "#define SG_COUNTS_BINDING %d\n#define SG_INDICES_BINDING %d\n"
        "#define SG_PARAMETERS_BINDING %d\n%s%s%s\n%s",
        RENDER_SHADER_RAMP_VEC4_MAX,
        entry->set.count,
        entry->set.count + 1,
        entry->set.count + 2,
        entry->set.count + 3,
        scene,
        uniforms,
        graphs,
        programs->body
    );
    CF_Shader draw = { 0 };
    CF_ComputeShader cs = { 0 };
    if (compute) {
        cs = cf_make_compute_shader_from_source(source);
    } else if (programs->kind == RENDER_PROGRAM_FRAGMENT) {
        draw = cf_make_draw_shader_from_source(source);
    } else {
        draw = cf_make_shader_from_source(programs->vertex, source);
    }
    const char *dump = getenv("CHIBI_SHADER_DUMP");
    if (!draw.id && !cs.id && dump && *dump) {
        FILE *file = fopen(dump, "wb");
        if (file) {
            fwrite(source, 1, strlen(source), file);
            fclose(file);
        }
    }
    free(graphs);
    free(source);
    if (!draw.id && !cs.id) {
        free(ramp_data);
        snprintf(entry->error, sizeof entry->error, "%s", cf_shader_compile_error());
        return false;
    }
    if (entry->draw.id) {
        cf_destroy_shader(entry->draw);
    }
    if (entry->compute.id) {
        cf_destroy_compute_shader(entry->compute);
    }
    entry->draw = draw;
    entry->compute = cs;
    free(entry->ramp_data);
    entry->ramp_data = ramp_data;
    entry->ramp_vec4_count = ramp_vec4_count;
    entry->ramp_revision = render_shader_ramp_revision();
    entry->error[0] = 0;
    return true;
}

unsigned render_shader_compile_count()
{
    return shader_compile_count;
}

int render_programs_prepare(render_programs *programs, const render_shader_set *set)
{
    render_images_collect();
    programs->error[0] = 0;
    if (!programs->body || (programs->kind == RENDER_PROGRAM_ARENA &&
                            (!programs->vertex || !programs->scene))) {
        snprintf(programs->error, sizeof programs->error, "Cannot read material shader sources.");
        return 0;
    }
    shader_error error = { 0 };
    for (int i = 0; i < set->count; i++) {
        if (!render_image_texture(set->images[i], &error).id) {
            snprintf(programs->error, sizeof programs->error, "%s", error.message);
            return 0;
        }
    }
    int index = 0;
    for (; index < acount(programs->entries); index++) {
        const render_shader_set *candidate = &programs->entries[index].set;
        if (candidate->count == set->count &&
            !memcmp(
                candidate->images,
                set->images,
                (size_t)set->count * sizeof *set->images
            )) {
            break;
        }
    }
    if (index == acount(programs->entries)) {
        apush(programs->entries, ((program){ .set = *set }));
    }
    program *entry = &programs->entries[index];
    unsigned current_revision = render_shader_revision();
    unsigned current_ramp_revision = render_shader_ramp_revision();
    if (entry->checked_revision != current_revision ||
        entry->checked_ramp_revision != current_ramp_revision) {
        entry->checked_ramp_count = render_shader_ramp_vec4_count(set);
        entry->checked_revision = current_revision;
        entry->checked_ramp_revision = current_ramp_revision;
    }
    if (entry->checked_ramp_count > RENDER_SHADER_RAMP_VEC4_MAX) {
        snprintf(
            programs->error,
            sizeof programs->error,
            "Included shader ramps need %d vec4 values; the limit is %d.",
            entry->checked_ramp_count,
            RENDER_SHADER_RAMP_VEC4_MAX
        );
        return entry->draw.id || entry->compute.id ? index + 1 : 0;
    }
    if (entry->revision != current_revision &&
        entry->attempted_revision != current_revision) {
        entry->attempted_revision = current_revision;
        if (compile(programs, entry)) {
            entry->revision = current_revision;
        }
    }
    if (entry->error[0]) {
        snprintf(programs->error, sizeof programs->error, "%s", entry->error);
        if (!entry->draw.id && !entry->compute.id) {
            return 0;
        }
    }
    return index + 1;
}

static void update_ramp_data(program *entry)
{
    if (entry->ramp_vec4_count && entry->revision == render_shader_revision() &&
        entry->ramp_revision != render_shader_ramp_revision()) {
        render_shader_ramp_payload(&entry->set, entry->ramp_data);
        entry->ramp_revision = render_shader_ramp_revision();
    }
}

const char *render_programs_error(const render_programs *programs)
{
    return programs->error;
}

void render_programs_bind3d(render_programs *programs, int index)
{
    program *entry = &programs->entries[index - 1];
    update_ramp_data(entry);
    for (int i = 0; i < entry->set.count; i++) {
        char name[32];
        snprintf(name, sizeof name, "sg_image_%d", i);
        cf_draw3d_set_texture(name, render_image_texture(entry->set.images[i], NULL));
    }
    if (entry->ramp_vec4_count) {
        cf_draw3d_set_uniform("sg_ramp_data", entry->ramp_data, CF_UNIFORM_TYPE_FLOAT4, RENDER_SHADER_RAMP_VEC4_MAX);
    }
    cf_draw3d_push_shader(entry->draw);
}

void render_programs_unbind3d(render_programs *programs, int index)
{
    program *entry = &programs->entries[index - 1];
    for (int i = 0; i < entry->set.count; i++) {
        char name[32];
        snprintf(name, sizeof name, "sg_image_%d", i);
        cf_draw3d_set_texture(name, (CF_Texture){ 0 });
    }
    cf_draw3d_pop_shader();
}

void render_programs_bind2d(render_programs *programs, int index)
{
    program *entry = &programs->entries[index - 1];
    update_ramp_data(entry);
    cf_draw_push_shader(entry->draw);
    for (int i = 0; i < entry->set.count; i++) {
        char name[32];
        snprintf(name, sizeof name, "sg_image_%d", i);
        cf_draw_set_texture(name, render_image_texture(entry->set.images[i], NULL));
    }
    if (entry->ramp_vec4_count) {
        cf_draw_set_uniform("sg_ramp_data", entry->ramp_data, CF_UNIFORM_TYPE_FLOAT4, RENDER_SHADER_RAMP_VEC4_MAX);
    }
}

void render_programs_unbind2d(render_programs *programs, int index)
{
    program *entry = &programs->entries[index - 1];
    for (int i = 0; i < entry->set.count; i++) {
        char name[32];
        snprintf(name, sizeof name, "sg_image_%d", i);
        RENDER_DRAW_UNBIND_TEXTURES(name);
    }
    cf_draw_pop_shader();
}

CF_ComputeShader render_programs_bind_compute(render_programs *programs, int index, CF_Material material)
{
    program *entry = &programs->entries[index - 1];
    update_ramp_data(entry);
    for (int i = 0; i < entry->set.count; i++) {
        char name[32];
        snprintf(name, sizeof name, "sg_image_%d", i);
        cf_material_set_texture_cs(
            material,
            name,
            render_image_texture(entry->set.images[i], NULL)
        );
    }
    if (entry->ramp_vec4_count) {
        cf_material_set_uniform_cs(material, "sg_ramp_data", entry->ramp_data, CF_UNIFORM_TYPE_FLOAT4, RENDER_SHADER_RAMP_VEC4_MAX);
    }
    return entry->compute;
}

void render_programs_free(render_programs *programs)
{
    if (!programs) {
        return;
    }
    for (int i = 0; i < acount(programs->entries); i++) {
        if (programs->entries[i].draw.id) {
            cf_destroy_shader(programs->entries[i].draw);
        }
        if (programs->entries[i].compute.id) {
            cf_destroy_compute_shader(programs->entries[i].compute);
        }
        free(programs->entries[i].ramp_data);
    }
    afree(programs->entries);
    free(programs->vertex);
    free(programs->body);
    free(programs->scene);
    free(programs);
}
