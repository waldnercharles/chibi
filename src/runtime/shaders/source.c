#include "runtime/shaders/source.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void shader_text_free(shader_text *text)
{
    free(text->path);
    free(text->entry);
    free(text->code);
    for (int i = 0; i < acount(text->includes); i++) {
        free(text->includes[i].path);
        free(text->includes[i].code);
    }
    afree(text->includes);
    *text = (shader_text){ 0 };
}

shader_text shader_text_clone(const shader_text *text)
{
    shader_text copy = { .path = xstrdup(text->path),
                         .entry = xstrdup(text->entry),
                         .code = xstrdup(text->code) };
    for (int i = 0; i < acount(text->includes); i++) {
        apush(
            copy.includes,
            ((shader_text_include){ xstrdup(text->includes[i].path),
                                    xstrdup(text->includes[i].code) })
        );
    }
    return copy;
}

static bool fail(shader_error *error, const char *message)
{
    if (error) {
        error->node = 0;
        snprintf(error->message, sizeof error->message, "%s", message);
    }
    return false;
}

static bool identifier(const char *name)
{
    if (!name || !*name || strlen(name) > 127 ||
        !(isalpha((unsigned char)*name) || *name == '_') ||
        !strncmp(name, "gl_", 3) || strstr(name, "__")) {
        return false;
    }
    for (const char *p = name + 1; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') {
            return false;
        }
    }
    return true;
}

static bool path_valid(const char *path)
{
    if (!path || !*path || strlen(path) > 1024 || *path == '/') {
        return false;
    }
    for (const char *p = path; *p; p++) {
        if ((unsigned char)*p < 32 || *p == '"' || *p == '\\' || *p == ':') {
            return false;
        }
        if ((p == path || p[-1] == '/') &&
            (*p == '/' || (*p == '.' && (!p[1] || p[1] == '/' ||
                                         (p[1] == '.' && (!p[2] || p[2] == '/')))))) {
            return false;
        }
    }
    return path[strlen(path) - 1] != '/';
}

bool shader_text_validate(
    const shader_text *text,
    char *const names[32],
    const int widths[32],
    shader_error *error
)
{
    if (!path_valid(text->path) || !identifier(text->entry) || !text->code ||
        !*text->code || acount(text->includes) > SHADER_TEXT_INCLUDE_MAX) {
        return fail(error, "Invalid particle GLSL source metadata");
    }
    if (!shader_particle_parameters_validate(names, widths, error)) {
        return false;
    }
    for (int i = 3; i < SHADER_PARAMETER_MAX; i++) {
        if (!widths[i] && !names[i]) {
            continue;
        }
        if (widths[i] < 1 || widths[i] > 4 || !identifier(names[i])) {
            return fail(error, "Particle GLSL parameters need identifiers and float/vector widths");
        }
        for (int j = 3; j < i; j++) {
            if (names[j] && !strcmp(names[i], names[j])) {
                return fail(error, "Duplicate particle GLSL parameter name");
            }
        }
    }
    size_t bytes = strlen(text->code);
    for (int i = 0; i < acount(text->includes); i++) {
        const shader_text_include *include = &text->includes[i];
        if (!path_valid(include->path) || !include->code ||
            !strcmp(include->path, text->path)) {
            return fail(error, "Invalid bundled GLSL include");
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(include->path, text->includes[j].path)) {
                return fail(error, "Duplicate bundled GLSL include");
            }
        }
        size_t length = strlen(include->code);
        if (length > SHADER_TEXT_BYTES_MAX || bytes > SHADER_TEXT_BYTES_MAX - length) {
            return fail(error, "Particle GLSL source exceeds ownership limit");
        }
        bytes += length;
    }
    return bytes <= SHADER_TEXT_BYTES_MAX ||
           fail(error, "Particle GLSL source exceeds ownership limit");
}

static void append(char **text, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    size_t old = *text ? strlen(*text) : 0;
    *text = xrealloc(*text, old + (size_t)count + 1);
    vsnprintf(*text + old, (size_t)count + 1, format, args);
    va_end(args);
}

char *shader_text_particle_glsl(
    const shader_text *text,
    char *const names[32],
    const int widths[32],
    shader_error *error
)
{
    if (!shader_text_validate(text, names, widths, error)) {
        return NULL;
    }
    char *source = NULL;
    append(&source, "#define SG_PARTICLE_OUTPUT_COUNT 6\n%s", shader_sample_glsl());
    append(
        &source,
        "struct ChibiParticleInput { float index; float count; float seed; "
        "float time; };\n"
        "struct ChibiParticle { vec3 position; float size; vec4 color; vec3 "
        "scale; vec3 rotation; float lighting; };\n"
        "ChibiParticle chibi_particle() { ChibiParticle p; "
        "p.position=vec3(0.0); p.size=1.0; p.color=vec4(1.0); "
        "p.scale=vec3(1.0); p.rotation=vec3(0.0); p.lighting=0.0; return p; }\n"
        "struct ChibiParameters {\n"
    );
    int count = 0;
    const char *types[] = { "", "float", "vec2", "vec3", "vec4" };
    for (int i = 3; i < SHADER_PARAMETER_MAX; i++) {
        if (widths[i]) {
            append(&source, "  %s %s;\n", types[widths[i]], names[i]);
            count++;
        }
    }
    if (!count) {
        append(&source, "  float chibi_unused;\n");
    }
    append(&source, "};\n#include \"%s\"\n", text->path);
    append(
        &source,
        "void sg_particle(SGSample s, vec4 parameters[32], out vec4 result[9]) "
        "{\n"
        "  ChibiParticleInput p; p.index=parameters[0].x; "
        "p.count=parameters[1].x; p.seed=parameters[2].x; p.time=s.time;\n"
        "  ChibiParameters u;\n"
    );
    const char *swizzles[] = { "", "x", "xy", "xyz", "xyzw" };
    for (int i = 3; i < SHADER_PARAMETER_MAX; i++) {
        if (widths[i]) {
            append(&source, "  u.%s=parameters[%d].%s;\n", names[i], i, swizzles[widths[i]]);
        }
    }
    if (!count) {
        append(&source, "  u.chibi_unused=0.0;\n");
    }
    append(
        &source,
        "  ChibiParticle value=%s(p,u);\n"
        "  result[0]=vec4(value.position,0.0); result[1]=vec4(value.size); "
        "result[2]=value.color;\n"
        "  result[3]=vec4(value.scale,0.0); "
        "result[4]=vec4(value.rotation,0.0); result[5]=vec4(value.lighting);\n"
        "}\n",
        text->entry
    );
    return source;
}
