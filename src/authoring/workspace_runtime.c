#include "authoring/workspace_runtime.h"

#include "assets/scene_document.h"
#include "authoring/workspace_presentation.h"
#include "authoring/workspace_runtime_evidence.h"
#include "authoring/workspace_runtime_model_internal.h"
#include "authoring/workspace_snapshot.h"
#include "host/authoring_platform.h"
#include "platform/cute.h"
#include "support/asset_json.h"
#include "support/rooted_files.h"

#include <SDL3/SDL.h>
#include <errno.h>
#ifdef __EMSCRIPTEN__
#include "host/browser_compiler.h"
#endif
#include "render/debug_geometry.h"
#include "render/geometry_2d.h"
#include "render/scene_renderer.h"
#include "render/shader_publish.h"
#include "render/ui.h"
#include "runtime/behaviors/scene_flow.h"
#include "runtime/camera.h"
#include "runtime/value_json.h"
#include "support/array.h"
#include "support/sha256.h"
#include "support/utf8.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct workspace_runtime
{
    char *input_timeline, *artifact_host_path;
    char artifact_sha256[65];
    size_t artifact_bytes;
    uint64_t ui_dispatch_serial;
    int ui_dispatch_status;
    workspace_runtime_config config;
    char *root, *compiler, *sdk_version, *sdk_build, *sdk_manifest, *artifact_relative;
    av_doc *trace;
    bool trace_truncated, untracked, drawn_current;
    int trace_count, drawn_width, drawn_height;
    size_t trace_bytes;
    engine_viewport_view drawn_view;
    render_scene3d_camera drawn_camera;
    bool drawn_perspective, drawn_random_isolated;
    uint64_t drawn_random_state;
    workspace_runtime_result drawn;
    char drawn_backend[64], drawn_driver[64];
    struct
    {
        int handle, next_handle, width, height;
        CF_Canvas canvas;
        CF_Readback readback;
        uint64_t started;
        workspace_runtime_evidence_plan *plan;
        char directory[1024], state[24], error[512];
    } evidence;
    workspace_snapshot *snapshot, *candidate;
    scene_document *source_origins, *pending_origins;
    char *pending_scene_path;
    const scene *origin_scene;
    bool virtual_origins, pending_virtual_origins;
    char *browser_root;
#ifdef __EMSCRIPTEN__
    browser_compiler *browser;
#endif
    authoring_platform *job;
    int request, next_session, session, document, revision, pending_document,
        pending_revision;
    player *player;
    app_scene_renderer *renderer;
    engine_viewport *viewport;
    uint64_t *hidden, *locked;
    double now;
    char *initial_scene;
    engine_viewport_view view;
    bool has_view, perspective, follow_camera_next_draw;
    float grid;
    struct
    {
        const scene *scene;
        int object, part;
        bool section;
        float normal[3], offset;
    } model_view;
    bool unlit, fogless, show_colliders, show_cameras;
    render_debug_geometry_result debug_geometry;
    uint64_t texture_revision, mutation_serial;
    bool render_failed;
    char error[512], status[48], log[AUTHORING_PLATFORM_LOG_MAX + 1];
};
typedef struct
{
    render_resource_scope render;
    declared_components_scope types;
    declared_components *components;
} runtime_scope;
static bool runtime_set_view(workspace_runtime *, int, const engine_viewport_view *, bool, bool);
static bool runtime_enter(render_resource_context *context, runtime_scope *scope, char *error, size_t size)
{
    if (!render_resource_scope_enter(context, &scope->render, error, size)) {
        return false;
    }
    if (declared_components_scope_enter(scope->components, &scope->types)) {
        return true;
    }
    if (!render_resource_scope_leave(&scope->render, error, size)) {
        abort();
    }
    if (size) {
        snprintf(error, size, "Cannot enter runtime type reflection");
    }
    return false;
}
static bool runtime_leave(runtime_scope *scope, char *error, size_t size)
{
    if (!declared_components_scope_leave(&scope->types)) {
        if (size) {
            snprintf(error, size, "Cannot restore runtime type reflection");
        }
        return false;
    }
    return render_resource_scope_leave(&scope->render, error, size);
}
static workspace_runtime_result result(workspace_runtime *s, bool ok)
{
    world *w = s->player ? player_world(s->player) : NULL;
    engine *e = w ? world_engine(w) : NULL;
    const engine_frame_timing *timing = e ? engine_last_timing(e) : NULL;
    return (workspace_runtime_result){
        .ok = ok,
        .preparing = s->request > 0,
        .ready = s->player != NULL,
        .playing = e && engine_is_playing(e),
        .session = s->session,
        .document = s->document,
        .revision = s->revision,
        .steps = timing ? timing->simulation_steps : 0,
        .time = timing ? timing->simulation_time : 0,
        .texture = s->viewport ? engine_viewport_texture(s->viewport) : 0,
        .texture_revision = s->texture_revision,
        .status = s->status,
        .error = s->error,
        .log = s->log
    };
}
static av_value *trace_event(workspace_runtime *s, const char *operation)
{
    s->drawn_current = false;
    if (s->mutation_serial < UINT64_MAX) {
        s->mutation_serial++;
    } else {
        s->untracked = true;
    }
    if (!s->trace || s->trace_count >= 4096 || s->trace_bytes >= 1024u * 1024u - 4096) {
        s->trace_truncated = true;
        return NULL;
    }
    av_value *row = av_obj(s->trace);
    av_arr_append(av_doc_root(s->trace), row);
    av_obj_add_str(s->trace, row, "operation", operation);
    av_obj_set(s->trace, row, "host_time", av_real(s->trace, s->now));
    const engine_frame_timing *timing = s->player
                                          ? engine_last_timing(
                                                world_engine(player_world(s->player))
                                            )
                                          : NULL;
    av_obj_add_uint(s->trace, row, "fixed_steps", timing ? timing->simulation_steps : 0);
    s->trace_count++;
    s->trace_bytes += 2048;
    return row;
}
static bool trace_details_valid(const av_value *v, int depth, int *nodes, size_t *bytes)
{
    if (!v || depth > 8 || ++*nodes > 256) {
        return false;
    }
    if (av_is_str(v)) {
        *bytes += strlen(av_get_str(v));
    }
    if (av_is_obj(v) || av_is_arr(v)) {
        size_t count = av_is_obj(v) ? av_obj_size(v) : av_arr_size(v);
        if (count > 256) {
            return false;
        }
        for (size_t i = 0; i < count; i++) {
            if (av_is_obj(v)) {
                *bytes += strlen(av_get_str(av_obj_key_at(v, i)));
            }
            if (*bytes > 8192 ||
                !trace_details_valid(
                    av_is_obj(v) ? av_obj_value_at(v, i) : av_arr_get(v, i),
                    depth + 1,
                    nodes,
                    bytes
                )) {
                return false;
            }
        }
    }
    return *bytes <= 8192;
}
bool workspace_runtime_record_call(
    workspace_runtime *s,
    int session,
    const char *operation,
    const av_value *details
)
{
    int nodes = 0;
    size_t bytes = 0;
    if (!s || !s->player || session != s->session || !operation ||
        !operation[0] || strlen(operation) > 64 || !av_is_obj(details) ||
        !trace_details_valid(details, 0, &nodes, &bytes)) {
        return false;
    }
    av_value *record = trace_event(s, operation);
    if (record) {
        av_obj_set(s->trace, record, "details", av_clone(s->trace, details));
        s->trace_bytes += bytes;
    }
    return true;
}
const char *workspace_runtime_artifact_relative(const workspace_runtime *s)
{
    return s && s->artifact_relative ? s->artifact_relative : "";
}
const char *workspace_runtime_sdk_build(const workspace_runtime *s)
{
    return s ? s->sdk_build : "";
}
const char *workspace_runtime_sdk_manifest(const workspace_runtime *s)
{
    return s ? s->sdk_manifest : "";
}
void workspace_runtime_mark_external_change(workspace_runtime *s)
{
    if (s) {
        s->untracked = true;
        s->drawn_current = false;
        if (s->mutation_serial < UINT64_MAX) {
            s->mutation_serial++;
        }
    }
}
static void evidence_release(workspace_runtime *s)
{
    if (s->evidence.readback.id) {
        cf_destroy_readback(s->evidence.readback);
    }
    if (s->evidence.canvas.id) {
        cf_destroy_canvas(s->evidence.canvas);
    }
    s->evidence.readback = (CF_Readback){ 0 };
    s->evidence.canvas = (CF_Canvas){ 0 };
    workspace_runtime_evidence_free(s->evidence.plan);
    s->evidence.plan = NULL;
}
static workspace_runtime_result refuse(workspace_runtime *s, const char *status, const char *error)
{
    char message[512];
    snprintf(message, sizeof message, "%s", error);
    snprintf(s->status, sizeof s->status, "%s", status);
    snprintf(s->error, sizeof s->error, "%s", message);
    return result(s, false);
}
workspace_runtime *workspace_runtime_new(const workspace_runtime_config *config, char *error, size_t size)
{
    rooted_files *root_probe = config && config->root
                                 ? rooted_files_open(config->root)
                                 : NULL;
    char *canonical_root = root_probe ? rooted_files_host_path(root_probe) : NULL;
    bool root_valid = canonical_root != NULL;
    rooted_files_close(root_probe);
    if (!config || !config->documents || !root_valid || !config->host.linker) {
        if (size) {
            snprintf(error, size, "Runtime requires documents, canonical workspace root and prepared host linker");
        }
        free(canonical_root);
        return NULL;
    }
    workspace_runtime *s = xcalloc(1, sizeof *s);
    s->config = *config;
    s->root = canonical_root;
    s->compiler = xstrdup(config->compiler ? config->compiler : "");
    s->config.root = s->root;
    s->config.compiler = s->compiler;
    s->sdk_version = xstrdup(config->engine_version ? config->engine_version : "");
    s->sdk_build = xstrdup(config->build_id ? config->build_id : "");
    s->sdk_manifest = xstrdup(config->manifest_hash ? config->manifest_hash : "");
    s->config.engine_version = s->sdk_version;
    s->config.build_id = s->sdk_build;
    s->config.manifest_hash = s->sdk_manifest;
    snprintf(s->status, sizeof s->status, "empty");
    return s;
}
static void clear_renderer(workspace_runtime *s)
{
    if (!s->player) {
        return;
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    char error[128];
    render_resource_context *context = render_resource_context_for_world(
        player_world(s->player)
    );
    bool entered = runtime_enter(context, &scope, error, sizeof error);
    if (!entered) {
        abort();
    }
    if (s->config.ui_retire) {
        s->config.ui_retire(s->config.ui_userdata, s->viewport ? engine_viewport_ui(s->viewport) : NULL);
    }
    engine_viewport_free(s->viewport);
    s->viewport = NULL;
    app_scene_renderer_free(s->renderer);
    s->renderer = NULL;
    if (!runtime_leave(&scope, error, sizeof error)) {
        abort();
    }
}
static void clear_job(workspace_runtime *s)
{
#ifdef __EMSCRIPTEN__
    browser_compiler_free(s->browser);
    s->browser = NULL;
#endif
    authoring_platform_free(s->job);
    s->job = NULL;
    s->request = 0;
    scene_document_free(s->pending_origins);
    s->pending_origins = NULL;
    free(s->pending_scene_path);
    s->pending_scene_path = NULL;
    workspace_snapshot_free(s->candidate);
    s->candidate = NULL;
}
void workspace_runtime_free(workspace_runtime *s)
{
    if (!s) {
        return;
    }
    clear_job(s);
    scene_document_free(s->source_origins);
    evidence_release(s);
    clear_renderer(s);
    if (s->player) {
        player_free(s->player);
    }
    workspace_snapshot_free(s->snapshot);
    s->snapshot = NULL;
#ifdef __EMSCRIPTEN__
    browser_compiler_snapshot_free(s->browser_root);
#else
    free(s->browser_root);
#endif
    s->browser_root = NULL;
    afree(s->hidden);
    afree(s->locked);
    free(s->initial_scene);
    free(s->root);
    free(s->compiler);
    free(s->artifact_host_path);
    free(s->sdk_version);
    free(s->sdk_build);
    free(s->sdk_manifest);
    free(s->input_timeline);
    s->input_timeline = NULL;
    free(s->artifact_relative);
    av_doc_free(s->trace);
    free(s);
}
static void source_origins(workspace_runtime *s, const char *root_path)
{
    rooted_files *root = rooted_files_open(root_path);
    unsigned char *text = NULL;
    size_t bytes;
    char error[256];
    bool read = root && s->pending_scene_path &&
                rooted_files_read(root, s->pending_scene_path, 1024u * 1024u, &text, &bytes);
    rooted_files_close(root);
    scene_document_free(s->pending_origins);
    s->pending_origins = read ? scene_document_load((const char *)text, error, sizeof error)
                              : NULL;
    free(text);
}
static workspace_runtime_result prepare_source(
    workspace_runtime *s,
    int document,
    int revision,
    const char *virtual_path,
    const char *virtual_text,
    const char *const *paths,
    const char *const *sources,
    int count
)
{
    if (s->request) {
        return refuse(s, "busy", "A runtime candidate is already preparing");
    }
    if (s->next_session == INT_MAX) {
        return refuse(s, "limit", "Runtime session handles exhausted");
    }
    char *scene = NULL;
    if (virtual_path) {
        size_t n = strlen(virtual_path),
               bytes = virtual_text ? strlen(virtual_text) : 0;
        if (!n || n > 1024 || !player_relative_path(virtual_path) || n < 4 ||
            strcmp(virtual_path + n - 4, ".kdl") ||
            (virtual_text &&
             (bytes > 1024u * 1024u || !utf8_text_valid(virtual_text, bytes)))) {
            return refuse(s, "invalid", "Virtual scene requires an unused relative .kdl path and UTF-8 source <=1MiB");
        }
        scene = xstrdup(virtual_path);
    }
    if (document) {
        workspace_document_view view;
        workspace_document_status
            code = workspace_document_read(s->config.documents, document, &view);
        if (code != WORKSPACE_DOCUMENT_OK || view.revision != revision) {
            free(scene);
            return refuse(s, "stale", "Runtime source revision is stale");
        }
        if (!view.path[0] || !player_relative_path(view.path)) {
            free(scene);
            return refuse(s, "invalid", "Runtime scene must have a workspace-relative saved path");
        }
        if (!virtual_path) {
            scene = xstrdup(view.path);
        }
    }
#ifdef __EMSCRIPTEN__
    s->browser = browser_compiler_new(
        s->root,
        s->config.documents,
        s->error,
        sizeof s->error
    );
    bool accepted = s->browser && browser_compiler_start_sources(
                                      s->browser,
                                      scene ? scene : "",
                                      paths,
                                      sources,
                                      count,
                                      false,
                                      s->error,
                                      sizeof s->error
                                  );
    s->pending_scene_path = xstrdup(scene ? scene : "");
    s->pending_virtual_origins = virtual_text != NULL;
    free(scene);
    if (!accepted) {
        clear_job(s);
        snprintf(s->status, sizeof s->status, "compile-failed");
        return result(s, false);
    }
    s->request = 1;
    s->pending_document = document;
    s->pending_revision = revision;
    s->error[0] = s->log[0] = 0;
    snprintf(s->status, sizeof s->status, "preparing");
    return result(s, true);
#endif
    if (!s->compiler[0]) {
        free(scene);
        return refuse(s, "unavailable", "Host has no compiler for frozen source previews");
    }
    s->candidate = workspace_snapshot_new_sources(
        s->root,
        s->config.documents,
        paths,
        sources,
        count,
        s->error,
        sizeof s->error
    );
    if (!s->candidate) {
        free(scene);
        snprintf(s->status, sizeof s->status, "snapshot-failed");
        return result(s, false);
    }
    authoring_platform_config cfg = { .root = workspace_snapshot_root(s->candidate),
                                      .compiler = s->compiler };
    s->job = authoring_platform_new(&cfg, s->error, sizeof s->error);
    if (!s->job) {
        free(scene);
        clear_job(s);
        return refuse(s, "unavailable", "Cannot create isolated compiler job");
    }
    authoring_platform_result
        job = authoring_platform_request(s->job, "compile", scene ? scene : "", 0, 0, 0);
    s->pending_scene_path = xstrdup(scene ? scene : "");
    s->pending_virtual_origins = virtual_text != NULL;
    free(scene);
    if (!job.ok) {
        char message[512];
        snprintf(message, sizeof message, "%s", job.message);
        clear_job(s);
        return refuse(s, "compile-failed", message);
    }
    s->request = job.request;
    s->pending_document = document;
    s->pending_revision = revision;
    s->error[0] = s->log[0] = 0;
    snprintf(s->status, sizeof s->status, "preparing");
    return result(s, true);
}
workspace_runtime_result workspace_runtime_prepare(workspace_runtime *s, int document, int revision)
{
    return prepare_source(s, document, revision, NULL, NULL, NULL, NULL, 0);
}
workspace_runtime_result workspace_runtime_prepare_scene_source(
    workspace_runtime *s,
    const char *relative_scene,
    const char *source
)
{
    if (!relative_scene || !source) {
        return refuse(s, "invalid", "Virtual scene path and source are required");
    }
    const char *paths[] = { relative_scene }, *sources[] = { source };
    return prepare_source(s, 0, 0, relative_scene, source, paths, sources, 1);
}
workspace_runtime_result workspace_runtime_prepare_document_scene_source(
    workspace_runtime *s,
    int document,
    int revision,
    const char *relative_scene,
    const char *source
)
{
    if (document <= 0 || revision <= 0 || !relative_scene || !source) {
        return refuse(s, "invalid", "Virtual scene requires a base document revision, path and source");
    }
    const char *paths[] = { relative_scene }, *sources[] = { source };
    return prepare_source(s, document, revision, relative_scene, source, paths, sources, 1);
}
workspace_runtime_result workspace_runtime_prepare_document_sources(
    workspace_runtime *s,
    int document,
    int revision,
    const char *scene,
    const char *const *paths,
    const char *const *sources,
    int count
)
{
    if (document <= 0 || revision <= 0 || !scene || count < 0 || count > 16 ||
        (count && (!paths || !sources))) {
        return refuse(s, "invalid", "Private sources require a base document revision, scene path and at most 16 sources");
    }
    const char *text = NULL;
    for (int i = 0; i < count; i++) {
        if (!paths[i] || !sources[i]) {
            return refuse(s, "invalid", "Private source path and text are required");
        }
        if (!strcmp(paths[i], scene)) {
            text = sources[i];
        }
    }
    return prepare_source(s, document, revision, scene, text, paths, sources, count);
}
workspace_runtime_result workspace_runtime_cancel(workspace_runtime *s)
{
    if (!s->request) {
        return result(s, true);
    }
    if (s->job) {
        authoring_platform_cancel(s->job, s->request);
    }
    clear_job(s);
    s->error[0] = 0;
    snprintf(s->status, sizeof s->status, "cancelled");
    return result(s, true);
}
static player *read_player(
    workspace_runtime *s,
    const char *path,
    const player_host *host,
    char hash[65],
    size_t *length,
    char **canonical
)
{
    char *leaf = NULL;
    rooted_files *root = path ? rooted_files_open_parent(path, &leaf) : NULL;
    unsigned char *data = NULL;
    size_t bytes = 0;
    bool readable = root &&
                    rooted_files_read(root, leaf, PLAYER_PREPARED_MAX_BYTES, &data, &bytes);
    char *directory = readable ? rooted_files_host_path(root) : NULL;
    rooted_files_close(root);
    if (!readable || !directory) {
        free(data);
        free(leaf);
        free(directory);
        snprintf(s->error, sizeof s->error, "Prepared executable must be a bounded regular file");
        return NULL;
    }
    player *next = player_prepared_read(
        data,
        bytes,
        host,
        s->config.backend,
        s->error,
        sizeof s->error
    );
    if (next) {
        sha256_hex(data, bytes, hash);
        *length = bytes;
        size_t size = strlen(directory) + strlen(leaf) + 2;
        *canonical = xmalloc(size);
        snprintf(*canonical, size, "%s/%s", directory, leaf);
    }
    free(data);
    free(leaf);
    free(directory);
    return next;
}
const char *workspace_runtime_artifact_sha256(const workspace_runtime *s)
{
    return s ? s->artifact_sha256 : "";
}
size_t workspace_runtime_artifact_bytes(const workspace_runtime *s)
{
    return s ? s->artifact_bytes : 0;
}
bool workspace_runtime_verify_artifact(const workspace_runtime *s, int session, char *error, size_t size)
{
    bool valid = s && s->player && s->session == session &&
                 s->artifact_host_path && s->artifact_sha256[0] &&
                 s->artifact_bytes <= PLAYER_PREPARED_MAX_BYTES;
    char *leaf = NULL;
    rooted_files *root = valid ? rooted_files_open_parent(s->artifact_host_path, &leaf)
                               : NULL;
    unsigned char *data = NULL;
    size_t bytes = 0;
    char hash[65] = "";
    bool read = root &&
                rooted_files_read(root, leaf, PLAYER_PREPARED_MAX_BYTES, &data, &bytes);
    rooted_files_close(root);
    free(leaf);
    if (read) {
        sha256_hex(data, bytes, hash);
    }
    free(data);
    bool same = read && bytes == s->artifact_bytes &&
                !strcmp(hash, s->artifact_sha256);
    if (!same && size) {
        snprintf(error, size, "Loaded executable changed, is unavailable, or session is stale");
    }
    return same;
}
static void adopt(
    workspace_runtime *s,
    player *next,
    int document,
    int revision,
    const char *path,
    const char hash[65],
    size_t bytes
)
{
    clear_renderer(s);
    if (s->player) {
        player_free(s->player);
    }
    workspace_snapshot_free(s->snapshot);
    s->snapshot = NULL;
#ifdef __EMSCRIPTEN__
    browser_compiler_snapshot_free(s->browser_root);
#else
    free(s->browser_root);
#endif
    s->browser_root = NULL;
    afree(s->hidden);
    afree(s->locked);
    s->model_view.scene = NULL;
    s->player = next;
    scene_document_free(s->source_origins);
    s->source_origins = s->pending_origins;
    s->pending_origins = NULL;
    s->virtual_origins = s->pending_virtual_origins;
    s->origin_scene = scene_instance_scene(player_scene(next));
    free(s->artifact_host_path);
    s->artifact_host_path = xstrdup(path);
    snprintf(s->artifact_sha256, sizeof s->artifact_sha256, "%s", hash);
    s->artifact_bytes = bytes;
    free(s->initial_scene);
    s->initial_scene = xstrdup(scene_flow_current(world_scenes(player_world(next))));
    s->session = ++s->next_session;
    s->document = document;
    s->revision = revision;
    s->now = 0;
    s->drawn_current = false;
    av_doc_free(s->trace);
    s->trace = av_doc_new();
    av_doc_set_root(s->trace, av_arr(s->trace));
    s->trace_count = 0;
    s->trace_bytes = 0;
    s->trace_truncated = false;
    s->untracked = false;
    free(s->input_timeline);
    s->input_timeline = NULL;
    free(s->artifact_relative);
    s->artifact_relative = NULL;
    s->texture_revision++;
    engine_set_playing(world_engine(player_world(next)), false);
}
workspace_runtime_result workspace_runtime_poll(workspace_runtime *s)
{
    if (!s->request) {
        return result(s, true);
    }
    if (s->pending_document) {
        workspace_document_view view;
        if (workspace_document_read(s->config.documents, s->pending_document, &view) !=
                WORKSPACE_DOCUMENT_OK ||
            view.revision != s->pending_revision) {
            clear_job(s);
            return refuse(s, "stale", "Runtime source revision changed while preparing");
        }
    }
#ifdef __EMSCRIPTEN__
    if (s->browser) {
        browser_compiler_result job = browser_compiler_poll(s->browser);
        snprintf(s->log, sizeof s->log, "%s", job.log);
        if (job.state == BROWSER_COMPILER_RUNNING) {
            return result(s, true);
        }
        if (job.state != BROWSER_COMPILER_SUCCEEDED) {
            char message[512];
            snprintf(message, sizeof message, "%s", job.error);
            clear_job(s);
            return refuse(s, "compile-failed", message);
        }
        player_host host = s->config.host;
        host.detached_types = true;
        host.resources = render_prepared_resources_detached(job.root);
        char hash[65], *canonical = NULL;
        size_t loaded_bytes = 0;
        player *next = read_player(s, job.artifact, &host, hash, &loaded_bytes, &canonical);
        if (!next) {
            clear_job(s);
            snprintf(s->status, sizeof s->status, "decode-failed");
            return result(s, false);
        }
        char *root = browser_compiler_take_root(s->browser);
        if (!root) {
            player_free(next);
            free(canonical);
            clear_job(s);
            return refuse(s, "compile-failed", "Frozen browser root ownership was lost");
        }
        source_origins(s, root);
        adopt(s, next, s->pending_document, s->pending_revision, canonical, hash, loaded_bytes);
        free(canonical);
        s->browser_root = root;
        free(s->artifact_relative);
        s->artifact_relative = xstrdup(job.artifact + strlen(root) + 1);
        clear_job(s);
        s->error[0] = 0;
        snprintf(s->status, sizeof s->status, "ready");
        return result(s, true);
    }
#endif
    authoring_platform_tick(s->job);
    authoring_platform_result job = authoring_platform_poll(s->job, s->request);
    snprintf(s->log, sizeof s->log, "%s", job.log ? job.log : "");
    if (!strcmp(job.state, "pending") || !strcmp(job.state, "running")) {
        return result(s, true);
    }
    if (strcmp(job.state, "succeeded")) {
        char error[512];
        snprintf(error, sizeof error, "%s", job.message);
        clear_job(s);
        return refuse(s, "compile-failed", error);
    }
    player_host host = s->config.host;
    host.detached_types = true;
    host.resources = render_prepared_resources_detached(workspace_snapshot_root(s->candidate));
    char hash[65], *canonical = NULL;
    size_t loaded_bytes = 0;
    player *next = read_player(s, job.value, &host, hash, &loaded_bytes, &canonical);
    if (!next) {
        clear_job(s);
        snprintf(s->status, sizeof s->status, "decode-failed");
        return result(s, false);
    }
    source_origins(s, workspace_snapshot_root(s->candidate));
    adopt(s, next, s->pending_document, s->pending_revision, canonical, hash, loaded_bytes);
    free(canonical);
    s->snapshot = s->candidate;
    s->candidate = NULL;
    free(s->artifact_relative);
    s->artifact_relative = xstrdup(
        job.value + strlen(workspace_snapshot_root(s->snapshot)) + 1
    );
    clear_job(s);
    s->error[0] = 0;
    snprintf(s->status, sizeof s->status, "ready");
    return result(s, true);
}
workspace_runtime_result workspace_runtime_restart(
    workspace_runtime *s,
    int session,
    const player_restart_input *input,
    bool *admitted
)
{
    if (admitted) {
        *admitted = false;
    }
    if (!admitted || s->config.read_only_presentation) {
        return refuse(s, "read-only", "Static presentation does not restart gameplay");
    }
    if (!workspace_runtime_verify_artifact(s, session, s->error, sizeof s->error)) {
        return refuse(s, "stale", s->error);
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    char error[128] = "";
    if (!runtime_enter(render_resource_context_for_world(player_world(s->player)), &scope, error, sizeof error)) {
        return refuse(s, "scope", error);
    }
    bool valid = player_restart_scene_valid(
        s->player,
        s->initial_scene,
        input,
        s->error,
        sizeof s->error
    );
    if (valid) {
        clear_renderer(s);
        valid = player_restart_scene(
            s->player,
            s->initial_scene,
            input,
            admitted,
            s->error,
            sizeof s->error
        );
        if (*admitted) {
            s->now = 0;
            afree(s->hidden);
            afree(s->locked);
            av_value *record = trace_event(s, "seeded-restart");
            if (record) {
                av_obj_add_uint(s->trace, record, "seed", input->seed);
                av_obj_add_int(s->trace, record, "record_limit", input->record_limit);
                av_obj_add_int(s->trace, record, "replay_keys", input->replay_count);
                av_obj_add_bool(s->trace, record, "begin_succeeded", valid);
                av_obj_add_str(
                    s->trace,
                    record,
                    "initial_save",
                    !input->initial_save_json ? "preserve committed"
                    : !strcmp(input->initial_save_json, "{}")
                        ? "empty in-memory store"
                        : "explicit JSON baseline"
                );
            }
        }
    }
    if (!runtime_leave(&scope, error, sizeof error)) {
        return refuse(s, "scope", error);
    }
    if (!valid) {
        return refuse(s, "restart-failed", s->error);
    }
    s->error[0] = 0;
    snprintf(s->status, sizeof s->status, "ready");
    return result(s, true);
}
workspace_runtime_result workspace_runtime_control(workspace_runtime *s, int session, const char *op)
{
    if (s->config.read_only_presentation && op &&
        (strcmp(op, "play") == 0 || strcmp(op, "step") == 0 ||
         strcmp(op, "advance") == 0 || strcmp(op, "stop") == 0)) {
        return refuse(s, "read-only", "Static presentation does not execute gameplay");
    }

    if (!s->player || session != s->session) {
        return refuse(s, "stale", "Runtime session is stale");
    }
    if (!op || strnlen(op, 257) > 256) {
        return refuse(s, "invalid", "Runtime operation is missing");
    }
    engine *e = world_engine(player_world(s->player));
    av_value *record = trace_event(s, "control");
    if (record) {
        av_obj_add_str(s->trace, record, "action", op);
        s->trace_bytes += strlen(op);
    }
    if (!strcmp(op, "advance")) {
        runtime_scope scope = { .components = player_declared_components(s->player) };
        char error[128];
        if (!runtime_enter(
                render_resource_context_for_world(player_world(s->player)),
                &scope,
                error,
                sizeof error
            )) {
            return refuse(s, "scope", error);
        }
        s->now += ENGINE_FIXED_SECONDS;
        bool ok = engine_frame(e, s->now) >= 0;
        char *next = ok ? scene_flow_take(world_scenes(player_world(s->player))) : NULL;
        if (next) {
            ok = player_load_scene(s->player, next, s->error, sizeof s->error);
            free(next);
            if (s->renderer) {
                app_scene_renderer_reset(s->renderer);
            }
            afree(s->hidden);
            afree(s->locked);
        }
        if (!ok && engine_error(e)) {
            snprintf(s->error, sizeof s->error, "%s", engine_error(e));
        }
        if (!runtime_leave(&scope, error, sizeof error)) {
            return refuse(s, "scope", error);
        }
        if (!ok) {
            return refuse(s, "runtime-failed", s->error);
        }
    } else if (!strcmp(op, "play")) {
        engine_set_playing(e, true);
    } else if (!strcmp(op, "pause")) {
        engine_set_playing(e, false);
    } else if (!strcmp(op, "step")) {
        engine_set_playing(e, false);
        engine_step(e);
    } else if (!strcmp(op, "stop")) {
        runtime_scope scope = { .components = player_declared_components(s->player) };
        char error[128];
        if (!runtime_enter(
                render_resource_context_for_world(player_world(s->player)),
                &scope,
                error,
                sizeof error
            )) {
            return refuse(s, "scope", error);
        }
        engine_set_playing(e, false);
        const char *name = s->initial_scene;
        char *copy = xstrdup(name);
        bool ok = player_load_scene(s->player, copy, s->error, sizeof s->error);
        free(copy);
        if (ok && s->renderer) {
            app_scene_renderer_reset(s->renderer);
        }
        bool left = runtime_leave(&scope, error, sizeof error);
        if (!ok || !left) {
            return refuse(s, "runtime-failed", !left ? error : s->error);
        }
        afree(s->hidden);
        afree(s->locked);
    } else {
        return refuse(s, "invalid", "Unknown runtime operation");
    }
    s->error[0] = 0;
    snprintf(s->status, sizeof s->status, "ready");
    return result(s, true);
}
static int ui_payload(void *user, const char *name, render_ui_event_field *fields, int capacity)
{
    workspace_runtime *s = user;
    source_registry *sources = world_sources(player_world(s->player));
    execution_module_event_info event;
    if (!source_event(sources, name, &event) || event.field_count > capacity) {
        return -1;
    }
    for (int i = 0; i < event.field_count; i++) {
        execution_module_event_field_info field;
        if (!source_event_field(sources, name, i, &field)) {
            return -1;
        }
        fields[i] = (render_ui_event_field){ field.name, field.initial };
    }
    return event.field_count;
}
static int ui_dispatch(void *user, uint64_t owner, const char *name, execution_arguments args)
{
    workspace_runtime *s = user;
    world *w = player_world(s->player);
    s->untracked = true;
    s->ui_dispatch_serial++;
    av_value *record = trace_event(s, "target-ui-event");
    if (record) {
        av_obj_add_str(s->trace, record, "name", name);
        s->trace_bytes += strlen(name);
    }
    int status = source_dispatch(
        world_sources(w),
        ecs_entity_from_u64(owner),
        name,
        args.values,
        args.count,
        engine_last_timing(world_engine(w))->simulation_time,
        ENGINE_FIXED_SECONDS
    );
    if (status < 0) {
        s->ui_dispatch_status = status;
    }
    return status;
}
static bool ui_initializing(void *user)
{
    workspace_runtime *s = user;
    return entities_initializing(world_entities(player_world(s->player)));
}
static ui_world runtime_ui(workspace_runtime *s)
{
    world *w = player_world(s->player);
    const engine_modules *modules = world_modules(w);
    if (s->config.read_only_presentation) {
        return (ui_world){ .ecs = world_ecs(w),
                           .model = modules->ui,
                           .page = scene_ui(scene_instance_scene(player_scene(s->player))),
                           .read_only = true,
                           .preview = true,
                           .hidden = { s->hidden, acount(s->hidden) } };
    }
    return (ui_world){ .ecs = world_ecs(w),
                       .model = modules->ui,
                       .save = modules->save,
                       .page = scene_ui(scene_instance_scene(player_scene(s->player))),
                       .host = { .context = s,
                                 .payload = ui_payload,
                                 .dispatch = ui_dispatch,
                                 .initializing = ui_initializing },
                       .hidden = { s->hidden, acount(s->hidden) } };
}
static void render_scene(void *user)
{
    workspace_runtime *s = user;
    world *w = player_world(s->player);
    const engine_modules *modules = world_modules(w);
    engine *e = world_engine(w);
    app_scene_renderer_set_time(s->renderer, (float)engine_last_timing(e)->simulation_time);
    app_scene_renderer_set_grid(s->renderer, s->grid);
    const scene_instance *model_instance = player_scene(s->player);
    bool model_selected = s->model_view.scene == scene_instance_scene(model_instance) &&
                          s->model_view.object >= 0 &&
                          s->model_view.object <
                              scene_object_count(s->model_view.scene);
    app_scene_renderer_set_model_inspection(
        s->renderer,
        model_selected ? scene_instance_entity(model_instance, s->model_view.object)
                       : (ecs_entity){ 0 },
        model_selected ? s->model_view.part : -1,
        model_selected && s->model_view.section,
        s->model_view.normal,
        s->model_view.offset
    );

    app_scene_renderer_set_view(s->renderer, s->unlit, s->fogless);
    if (engine_viewport_perspective(s->viewport)) {
        render_scene3d_camera camera;
        engine_viewport_get_camera3d(s->viewport, &camera);
        s->render_failed = !app_scene_renderer_draw3d(
            s->renderer,
            world_ecs(w),
            modules->particles,
            &camera,
            s->viewport,
            e,
            (render_skip){ s->hidden, acount(s->hidden) }
        );
        if (!s->render_failed && (s->show_colliders || s->show_cameras)) {
            s->debug_geometry = render_debug_geometry_draw(
                world_ecs(w),
                e,
                s->viewport,
                s->show_colliders,
                s->show_cameras,
                (render_skip){ s->hidden, acount(s->hidden) }
            );
        }
        return;
    }
    s->render_failed = !app_scene_renderer_draw(
        s->renderer,
        world_ecs(w),
        modules->particles,
        s->viewport,
        e,
        (render_skip){ s->hidden, acount(s->hidden) }
    );
    if (!s->render_failed && (s->show_colliders || s->show_cameras)) {
        s->debug_geometry = render_debug_geometry_draw(
            world_ecs(w),
            e,
            s->viewport,
            s->show_colliders,
            s->show_cameras,
            (render_skip){ s->hidden, acount(s->hidden) }
        );
    }
}
workspace_runtime_result workspace_runtime_draw(
    workspace_runtime *s,
    int session,
    int width,
    int height,
    double seconds
)
{
    if (!s->player || session != s->session) {
        return refuse(s, "stale", "Runtime session is stale");
    }
    if ((s->config.read_only_presentation && seconds != 0) || width < 1 ||
        height < 1 || width > 2048 || height > 2048 || !isfinite(seconds) ||
        seconds < 0 || seconds > ENGINE_FRAME_SECONDS_MAX) {
        return refuse(s, "invalid", "Runtime draw requires dimensions 1..2048 and delta 0..0.25");
    }
    world *w = player_world(s->player);
    runtime_scope scope = { .components = player_declared_components(s->player) };
    char error[128];
    if (!runtime_enter(render_resource_context_for_world(w), &scope, error, sizeof error)) {
        return refuse(s, "scope", error);
    }
    if (!s->viewport) {
        s->viewport = engine_viewport_new(width, height);
        s->renderer = app_scene_renderer_new();
        engine_viewport_set_render_callback(s->viewport, render_scene, s);
        engine_viewport_set_runtime(s->viewport, world_engine(w));
        engine_viewport_set_perspective(
            s->viewport,
            scene_perspective(scene_instance_scene(player_scene(s->player)))
        );
        ui_world ui = runtime_ui(s);
        /* The prepared UI catalog supplies pages and resources; no source IO. */
        ui.page = scene_ui(scene_instance_scene(player_scene(s->player)));
        engine_viewport_set_ui(s->viewport, &ui, "");
        if (s->has_view) {
            engine_viewport_set_view(s->viewport, &s->view);
            engine_viewport_set_perspective(s->viewport, s->perspective);
        }
    }
    av_value *record = trace_event(s, "draw");
    if (record) {
        av_obj_set(s->trace, record, "seconds", av_real(s->trace, seconds));
    }
    s->now += seconds;
    bool ok = s->config.read_only_presentation ||
              engine_frame(world_engine(w), s->now) >= 0;
    if (ok && !s->config.read_only_presentation) {
        char *next = scene_flow_take(world_scenes(w));
        if (next) {
            ok = player_load_scene(s->player, next, s->error, sizeof s->error);
            free(next);
            if (ok) {
                app_scene_renderer_reset(s->renderer);
                ui_world ui = runtime_ui(s);
                engine_viewport_set_ui(s->viewport, &ui, "");
                engine_viewport_set_perspective(
                    s->viewport,
                    scene_perspective(scene_instance_scene(player_scene(s->player)))
                );
                afree(s->hidden);
                afree(s->locked);
            }
        }
    }
    s->render_failed = false;
    ui_world current_ui = runtime_ui(s);
    engine_viewport_set_ui(s->viewport, &current_ui, "");
    engine_viewport_set_time(s->viewport, s->now);
    if (s->follow_camera_next_draw) {
        engine_viewport_view camera;
        if (camera_view(world_ecs(w), world_engine(w), &camera) == 1) {
            runtime_set_view(
                s,
                s->session,
                &camera,
                scene_perspective(scene_instance_scene(player_scene(s->player))),
                false
            );
        }
        s->follow_camera_next_draw = false;
    }
    uint64_t rendered_serial = s->mutation_serial;
    if (ok) {
        ok = engine_viewport_render(s->viewport, (float)width, (float)height) >= 0 &&
             !s->render_failed;
    }
    if (!ok && !s->error[0]) {
        snprintf(
            s->error,
            sizeof s->error,
            "%s",
            s->render_failed ? app_scene_renderer_error(s->renderer)
            : engine_error(world_engine(w)) ? engine_error(world_engine(w))
                                            : "Runtime frame failed"
        );
    }
    if (!runtime_leave(&scope, error, sizeof error)) {
        return refuse(s, "scope", error);
    }
    if (!ok) {
        snprintf(s->status, sizeof s->status, "runtime-failed");
        return result(s, false);
    }
    s->texture_revision++;
    s->drawn_current = rendered_serial == s->mutation_serial &&
                       rendered_serial != UINT64_MAX;
    s->drawn = result(s, true);
    engine_viewport_pixel_size(s->viewport, &s->drawn_width, &s->drawn_height);
    engine_viewport_get_view(s->viewport, &s->drawn_view);
    engine_viewport_camera drawn_frame;
    engine_viewport_get_camera(s->viewport, &drawn_frame);
    s->drawn_view.center_x = drawn_frame.center_x;
    s->drawn_view.center_y = drawn_frame.center_y;
    s->drawn_view.width = drawn_frame.span_width;
    s->drawn_view.height = drawn_frame.span_height;
    s->drawn_view.lens = drawn_frame.lens;
    engine_viewport_get_camera3d(s->viewport, &s->drawn_camera);
    s->drawn_perspective = engine_viewport_perspective(s->viewport);
    const behavior_random *random = engine_random_stream(world_engine(w));
    s->drawn_random_isolated = random && random->initialized;
    s->drawn_random_state = random ? random->state : 0;
    if (record) {
        av_obj_add_int(s->trace, record, "width", s->drawn_width);
        av_obj_add_int(s->trace, record, "height", s->drawn_height);
        av_obj_add_bool(s->trace, record, "perspective", s->drawn_perspective);
        av_value *view = av_obj_add_arr(s->trace, record, "view_matrix"),
                 *projection = av_obj_add_arr(s->trace, record, "projection_matrix");
        for (int i = 0; i < 16; i++) {
            av_arr_append(view, av_float(s->trace, s->drawn_camera.view.elements[i]));
            av_arr_append(
                projection,
                av_float(s->trace, s->drawn_camera.projection.elements[i])
            );
        }
        av_obj_add_uint(s->trace, record, "resulting_steps", s->drawn.steps);
        av_obj_set(
            s->trace,
            record,
            "simulation_time",
            av_real(s->trace, s->drawn.time)
        );
    }
    snprintf(s->drawn_backend, sizeof s->drawn_backend, "%s", cf_backend_type_to_string(cf_query_backend()));
    const char *driver = SDL_GetCurrentVideoDriver();
    snprintf(s->drawn_driver, sizeof s->drawn_driver, "%s", driver ? driver : "");
    s->error[0] = 0;
    snprintf(s->status, sizeof s->status, "ready");
    return result(s, true);
}
int workspace_runtime_camera(
    workspace_runtime *s,
    int session,
    engine_viewport_view *out,
    char *error,
    size_t size
)
{
    if (!s->player || session != s->session || !out) {
        if (size) {
            snprintf(error, size, "Runtime camera session is stale");
        }
        return -1;
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    if (!runtime_enter(render_resource_context_for_world(player_world(s->player)), &scope, error, size)) {
        return -1;
    }
    int code = camera_view(
        world_ecs(player_world(s->player)),
        world_engine(player_world(s->player)),
        out
    );
    if (!runtime_leave(&scope, error, size)) {
        return -1;
    }
    if (size) {
        snprintf(
            error,
            size,
            "%s",
            code < 0
                ? "Authored camera is invalid or multiple cameras are active"
            : code == 0 ? "No active authored camera"
                        : ""
        );
    }
    return code;
}
render_debug_geometry_result workspace_runtime_debug_geometry_status(const workspace_runtime *s)
{
    return s ? s->debug_geometry : (render_debug_geometry_result){ .invalid = true };
}
bool workspace_runtime_presentation(
    workspace_runtime *s,
    int session,
    bool unlit,
    bool fogless,
    bool colliders,
    bool cameras
)
{
    if (!s->player || session != s->session) {
        return false;
    }
    if (s->unlit == unlit && s->fogless == fogless &&
        s->show_colliders == colliders && s->show_cameras == cameras) {
        return true;
    }
    s->unlit = unlit;
    s->fogless = fogless;
    s->show_colliders = colliders;
    s->show_cameras = cameras;
    s->debug_geometry = (render_debug_geometry_result){ 0 };
    av_value *record = trace_event(s, "presentation");
    if (record) {
        av_obj_add_bool(s->trace, record, "unlit", unlit);
        av_obj_add_bool(s->trace, record, "fogless", fogless);
        av_obj_add_bool(s->trace, record, "colliders", colliders);
        av_obj_add_bool(s->trace, record, "cameras", cameras);
    }
    return true;
}
static bool runtime_set_view(
    workspace_runtime *s,
    int session,
    const engine_viewport_view *view,
    bool perspective,
    bool orbit_default
)
{
    if (!s->player || session != s->session || !view ||
        !isfinite(view->center_x) || !isfinite(view->center_y) ||
        !isfinite(view->width) || !isfinite(view->height) ||
        !isfinite(view->yaw) || !isfinite(view->pitch) || view->width <= 0 ||
        view->height <= 0 || view->width > 1e6f || view->height > 1e6f) {
        return false;
    }
    if (!isfinite(view->focal_distance) || view->focal_distance < 0 ||
        (view->focal_distance > 0 && view->focal_distance <= 1) ||
        !isfinite(view->roll) || !isfinite(view->target_elevation)) {
        return false;
    }
    const engine_viewport_lens *lens = &view->lens;
    const float values[] = { lens->eye.x,     lens->eye.y,     lens->eye.z,
                             lens->forward.x, lens->forward.y, lens->forward.z,
                             lens->up.x,      lens->up.y,      lens->up.z,
                             lens->fov,       lens->near_clip, lens->far_clip };
    for (size_t i = 0; i < sizeof values / sizeof *values; i++) {
        if (!isfinite(values[i])) {
            return false;
        }
    }
    if (lens->fov < 0 || lens->fov >= CF_PI || lens->near_clip < 0 ||
        lens->far_clip < 0 || ((lens->near_clip > 0) != (lens->far_clip > 0)) ||
        (lens->near_clip > 0 &&
         (lens->far_clip <= lens->near_clip ||
          lens->far_clip / lens->near_clip > ENGINE_VIEWPORT_CLIP_RATIO_MAX))) {
        return false;
    }
    float direction = cf_dot_v3(lens->forward, lens->forward);
    if (!isfinite(direction)) {
        return false;
    }
    if (direction > 0) {
        float up = cf_dot_v3(lens->up, lens->up),
              alignment = cf_dot_v3(lens->forward, lens->up);
        if (!isfinite(up) || !isfinite(alignment) || up <= 0 ||
            direction * up - alignment * alignment <= 1e-12f) {
            return false;
        }
    } else if (orbit_default && perspective && fabsf(view->pitch) > 1.5f) {
        return false;
    }
    s->drawn_current = false;
    if (s->mutation_serial < UINT64_MAX) {
        s->mutation_serial++;
    }
    if (orbit_default) {
        s->follow_camera_next_draw = false;
    }
    s->view = *view;
    if (orbit_default && perspective &&
        cf_dot_v3(s->view.lens.forward, s->view.lens.forward) == 0) {
        float distance = view->height * 0.5f / tanf(ENGINE_VIEWPORT_FOV_DEFAULT * 0.5f);
        CF_V3 offset = { sinf(view->yaw) * cosf(view->pitch) * distance,
                         sinf(view->pitch) * distance,
                         cosf(view->yaw) * cosf(view->pitch) * distance };
        s->view.lens.eye = cf_add_v3(
            (CF_V3){ view->center_x, view->target_elevation, -view->center_y },
            offset
        );
        s->view.lens.forward = cf_neg_v3(offset);
        s->view.lens.up = (CF_V3){ 0, 1, 0 };
    }
    s->has_view = true;
    s->perspective = perspective;
    if (s->viewport) {
        engine_viewport_set_view(s->viewport, &s->view);
        engine_viewport_set_perspective(s->viewport, perspective);
    }
    return true;
}
bool workspace_runtime_set_view(workspace_runtime *s, int session, const engine_viewport_view *view, bool perspective)
{
    return runtime_set_view(s, session, view, perspective, true);
}
bool workspace_runtime_apply_camera(workspace_runtime *s, int session, char *error, size_t size)
{
    engine_viewport_view view;
    int found = workspace_runtime_camera(s, session, &view, error, size);
    if (found != 1) {
        return false;
    }
    bool perspective = scene_perspective(scene_instance_scene(player_scene(s->player)));
    bool ok = runtime_set_view(s, session, &view, perspective, false);
    if (ok) {
        s->follow_camera_next_draw = true;
    }
    if (!ok && size) {
        snprintf(error, size, "Authored camera cannot be applied to the runtime view");
    }
    return ok;
}
static bool projection_ready(workspace_runtime *s, int session, char *error, size_t size)
{
    bool ready = s->player && session == s->session && s->drawn.session == session &&
                 s->drawn_width > 0 && s->drawn_height > 0;
    if (!ready && size) {
        snprintf(error, size, "Draw the current runtime session before querying its image camera");
    }
    return ready;
}
bool workspace_runtime_ray(workspace_runtime *s, int session, CF_V2 ndc, CF_Ray3 *out, char *error, size_t size)
{
    if (!out || !isfinite(ndc.x) || !isfinite(ndc.y) || fabsf(ndc.x) > 1 ||
        fabsf(ndc.y) > 1) {
        if (size) {
            snprintf(error, size, "Runtime ray requires finite normalized image coordinates");
        }
        return false;
    }
    if (!projection_ready(s, session, error, size)) {
        return false;
    }
    CF_Ray3 ray = s->drawn_perspective
                    ? render_scene3d_ray(&s->drawn_camera, ndc)
                    : (CF_Ray3){ .p = { s->drawn_view.center_x +
                                            ndc.x * s->drawn_view.width * .5f,
                                        1,
                                        -(s->drawn_view.center_y +
                                          ndc.y * s->drawn_view.height * .5f) },
                                 .d = { 0, -1, 0 },
                                 .t = 1e6f };
    if (!isfinite(ray.p.x) || !isfinite(ray.p.y) || !isfinite(ray.p.z) ||
        !isfinite(ray.d.x) || !isfinite(ray.d.y) || !isfinite(ray.d.z) ||
        !isfinite(ray.t) || ray.t <= 0) {
        if (size) {
            snprintf(error, size, "Runtime image camera ray is invalid");
        }
        return false;
    }
    *out = ray;
    return true;
}
bool workspace_runtime_project(
    workspace_runtime *s,
    int session,
    CF_V3 point,
    CF_V3 *out,
    bool *visible,
    char *error,
    size_t size
)
{
    if (!out || !visible || !isfinite(point.x) || !isfinite(point.y) ||
        !isfinite(point.z)) {
        if (size) {
            snprintf(error, size, "Runtime projection requires a finite world point");
        }
        return false;
    }
    if (!projection_ready(s, session, error, size)) {
        return false;
    }
    CF_V3 ndc = s->drawn_perspective
                  ? render_scene3d_project(&s->drawn_camera, point)
                  : (CF_V3){ (point.x - s->drawn_view.center_x) * 2 /
                                 s->drawn_view.width,
                             (-point.z - s->drawn_view.center_y) * 2 /
                                 s->drawn_view.height,
                             .5f };
    if (!isfinite(ndc.x) || !isfinite(ndc.y) || !isfinite(ndc.z)) {
        if (size) {
            snprintf(error, size, "Runtime point cannot be projected");
        }
        return false;
    }
    *out = ndc;
    *visible = ndc.z >= 0 && ndc.z <= 1 && fabsf(ndc.x) <= 1 && fabsf(ndc.y) <= 1;
    return true;
}
bool workspace_runtime_pick_subject(
    workspace_runtime *s,
    int session,
    float x,
    float y,
    int *object,
    char subject[64],
    char *error,
    size_t size
)
{
    *object = -1;
    subject[0] = 0;
    if (!projection_ready(s, session, error, size)) {
        return false;
    }
    if (!s->viewport || session != s->session || !isfinite(x) || !isfinite(y) ||
        fabsf(x) > 1 || fabsf(y) > 1) {
        if (size) {
            snprintf(error, size, "Runtime pick requires current session and normalized coordinates");
        }
        return false;
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    if (!runtime_enter(render_resource_context_for_world(player_world(s->player)), &scope, error, size)) {
        return false;
    }
    render_scene3d_camera camera = s->drawn_camera;
    ecs_entity entity;
    if (s->drawn_perspective) {
        entity = render_scene3d_pick(
            world_ecs(player_world(s->player)),
            &camera,
            (CF_V2){ x, y },
            (render_skip){ s->locked, acount(s->locked) }
        );
    } else {
        engine_viewport_view view = s->drawn_view;
        entity = render_geometry_2d_pick(
            world_ecs(player_world(s->player)),
            (CF_V2){ view.center_x + x * view.width * .5f,
                     -(view.center_y + y * view.height * .5f) },
            (float)s->drawn.time,
            (render_skip){ s->locked, acount(s->locked) }
        );
    }
    runtime_subject_token(entity, subject);
    const scene_instance *scene = player_scene(s->player);
    int count = scene_object_count(scene_instance_scene(scene));
    for (int i = 0; i < count; i++) {
        if (ecs_entity_to_u64(scene_instance_entity(scene, i)) ==
            ecs_entity_to_u64(entity)) {
            *object = i;
            break;
        }
    }
    return runtime_leave(&scope, error, size);
}
bool workspace_runtime_pick(
    workspace_runtime *s,
    int session,
    float x,
    float y,
    int *object,
    char *error,
    size_t size
)
{
    char subject[64];
    return workspace_runtime_pick_subject(s, session, x, y, object, subject, error, size);
}
bool workspace_runtime_skip(workspace_runtime *s, int session, const int *objects, int count, bool picking)
{
    if (!s->player || s->session != session || count < 0 || count > 4096 ||
        (count && !objects)) {
        return false;
    }
    const scene_instance *scene = player_scene(s->player);
    int total = scene_object_count(scene_instance_scene(scene));
    for (int i = 0; i < count; i++) {
        if (objects[i] < 0 || objects[i] >= total) {
            return false;
        }
    }
    av_value *record = NULL;
    if (s->trace_bytes + (size_t)count * 12 + 2048 <= 1024u * 1024u) {
        record = trace_event(s, "exclude");
    } else {
        s->trace_truncated = true;
        s->drawn_current = false;
    }
    if (record) {
        av_obj_add_bool(s->trace, record, "picking", picking);
        av_value *indices = av_obj_add_arr(s->trace, record, "objects");
        for (int i = 0; i < count; i++) {
            av_arr_append(indices, av_sint(s->trace, objects[i]));
        }
        s->trace_bytes += (size_t)count * 12;
    }
    s->drawn_current = false;
    uint64_t **list = picking ? &s->locked : &s->hidden;
    asetlen(*list, 0);
    for (int i = 0; i < count; i++) {
        apush(*list, ecs_entity_to_u64(scene_instance_entity(scene, objects[i])));
    }
    qsort(*list, (size_t)acount(*list), sizeof **list, render_skip_compare);
    return true;
}
const scene_instance *workspace_runtime_scene(const workspace_runtime *s)
{
    return s->player ? player_scene(s->player) : NULL;
}
const world *workspace_runtime_world(const workspace_runtime *s)
{
    return s->player ? player_world(s->player) : NULL;
}

workspace_runtime_result workspace_runtime_input(
    workspace_runtime *s,
    int session,
    const char *action,
    float value,
    bool pressed
)
{
    if (s->config.read_only_presentation) {
        return refuse(s, "read-only", "Static presentation does not accept gameplay input");
    }

    if (!s->player || session != s->session) {
        return refuse(s, "stale", "Runtime session is stale");
    }
    if (!action || !action[0] || strlen(action) > 256 || !isfinite(value)) {
        return refuse(s, "invalid", "Input action and finite value required");
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    char error[128];
    if (!runtime_enter(render_resource_context_for_world(player_world(s->player)), &scope, error, sizeof error)) {
        return refuse(s, "scope", error);
    }
    av_value *record = trace_event(s, "input");
    if (record) {
        av_obj_add_str(s->trace, record, "action", action);
        av_obj_add_float(s->trace, record, "value", value);
        av_obj_add_bool(s->trace, record, "pressed", pressed);
        s->trace_bytes += strlen(action);
    }
    engine_input_failure failure = { 0 };
    int accepted = engine_dispatch_input(world_engine(player_world(s->player)), action, value, pressed, &failure);
    char message[512];
    snprintf(message, sizeof message, "%s", failure.message ? failure.message : "");
    if (!runtime_leave(&scope, error, sizeof error)) {
        return refuse(s, "scope", error);
    }
    if (accepted < 0) {
        return refuse(s, "input-failed", message);
    }
    s->error[0] = 0;
    return result(s, true);
}

const engine_frame_timing *workspace_runtime_timing(const workspace_runtime *s)
{
    return s->player ? engine_last_timing(world_engine(player_world(s->player))) : NULL;
}
bool workspace_runtime_lighting(const workspace_runtime *s, render_radiance_status *out)
{
    return s->renderer && app_scene_renderer_lighting_status(s->renderer, out);
}

workspace_runtime_result workspace_runtime_current(workspace_runtime *s)
{
    return result(s, true);
}
declared_components *workspace_runtime_types(const workspace_runtime *s)
{
    return s->player ? player_declared_components(s->player) : NULL;
}

bool workspace_runtime_grid(workspace_runtime *s, int session, float grid)
{
    if (session != s->session || !s->player || !isfinite(grid) || grid < 0 ||
        grid > 1e6f) {
        return false;
    }
    s->drawn_current = false;
    if (s->mutation_serial < UINT64_MAX) {
        s->mutation_serial++;
    }
    s->grid = grid;
    return true;
}
bool workspace_runtime_bounds(
    workspace_runtime *s,
    int session,
    const int *objects,
    int count,
    CF_V3 *min,
    CF_V3 *max,
    char *error,
    size_t size
)
{
    if (session != s->session || !s->player || count < 0 || count > 4096 ||
        (count && !objects)) {
        if (size) {
            snprintf(error, size, "Invalid runtime bounds query");
        }
        return false;
    }
    const scene_instance *instance = player_scene(s->player);
    int total = scene_object_count(scene_instance_scene(instance));
    if (!count && total > 4096) {
        if (size) {
            snprintf(error, size, "Bounds query exceeds 4096 entities");
        }
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (objects[i] < 0 || objects[i] >= total) {
            if (size) {
                snprintf(error, size, "Runtime bounds index is stale");
            }
            return false;
        }
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    if (!runtime_enter(render_resource_context_for_world(player_world(s->player)), &scope, error, size)) {
        return false;
    }
    bool found = false;
    for (int i = 0; i < (count ? count : total); i++) {
        CF_V3 corners[8];
        ecs_entity entity = scene_instance_entity(instance, count ? objects[i] : i);
        if (render_skip_has((render_skip){ s->hidden, acount(s->hidden) }, entity)) {
            continue;
        }
        if (s->perspective) {
            if (!render_scene3d_entity_bounds(world_ecs(player_world(s->player)), entity, corners)) {
                continue;
            }
        } else {
            CF_V2 low, high;
            if (!render_geometry_2d_bounds(world_ecs(player_world(s->player)), entity, &low, &high)) {
                continue;
            }
            for (int j = 0; j < 8; j++) {
                corners[j] = (CF_V3){ j & 1 ? high.x : low.x, 0, j & 2 ? high.y : low.y };
            }
        }
        for (int j = 0; j < 8; j++) {
            if (!isfinite(corners[j].x) || !isfinite(corners[j].y) ||
                !isfinite(corners[j].z)) {
                continue;
            }
            if (!found) {
                *min = *max = corners[j];
                found = true;
            } else {
                min->x = fminf(min->x, corners[j].x);
                min->y = fminf(min->y, corners[j].y);
                min->z = fminf(min->z, corners[j].z);
                max->x = fmaxf(max->x, corners[j].x);
                max->y = fmaxf(max->y, corners[j].y);
                max->z = fmaxf(max->z, corners[j].z);
            }
        }
    }
    if (!runtime_leave(&scope, error, size)) {
        return false;
    }
    if (!found && size) {
        snprintf(error, size, "No finite visual bounds to frame");
    }
    return found;
}

bool workspace_runtime_read_only(const workspace_runtime *s)
{
    return s->config.read_only_presentation;
}

render_ui *workspace_runtime_ui(const workspace_runtime *s)
{
    return s->viewport ? engine_viewport_ui(s->viewport) : NULL;
}

bool workspace_runtime_input_timeline(workspace_runtime *s, int session, const char *json)
{
    if (!s || !s->player || session != s->session ||
        (json &&
         (strlen(json) > 1024u * 1024u || !utf8_text_valid(json, strlen(json))))) {
        return false;
    }
    char *copy = json ? xstrdup(json) : NULL;
    free(s->input_timeline);
    s->input_timeline = copy;
    return true;
}
bool workspace_runtime_ui_input(
    workspace_runtime *s,
    int session,
    const rml_input *input,
    unsigned *captured,
    bool *typing,
    bool *dispatched,
    char *error,
    size_t size
)
{
    render_ui *ui = s && !s->config.read_only_presentation ? workspace_runtime_ui(s)
                                                           : NULL;
    bool key = input && (input->type == RML_INPUT_KEY_PRESS ||
                         input->type == RML_INPUT_KEY_RELEASE);
    if (!s || !s->player || session != s->session || !ui || !s->drawn_width ||
        !s->drawn_height || !input || !captured || !typing || !dispatched ||
        input->type < RML_INPUT_POINTER_MOVE || input->type > RML_INPUT_TEXT_INPUT ||
        !isfinite(input->x) || !isfinite(input->y) ||
        !isfinite(input->wheel_x) || !isfinite(input->wheel_y) ||
        ((input->type == RML_INPUT_POINTER_DOWN || input->type == RML_INPUT_POINTER_UP) &&
         (input->button < 0 || input->button > 2)) ||
        (input->modifiers & ~127u) ||
        (key && (input->key <= RML_INPUT_KEY_UNKNOWN ||
                 input->key > RML_INPUT_KEY_RIGHT_META)) ||
        (input->type == RML_INPUT_TEXT_INPUT &&
         (!input->text || input->text_size > 4096 ||
          !utf8_text_valid(input->text, input->text_size)))) {
        if (size) {
            snprintf(error, size, "Invalid/stale target UI input or viewport not drawn");
        }
        return false;
    }
    rml_input value = *input;
    value.x = (input->x + 1) * .5f * s->drawn_width;
    value.y = (1 - input->y) * .5f * s->drawn_height;
    if (!isfinite(value.x) || !isfinite(value.y)) {
        if (size) {
            snprintf(error, size, "Target UI pointer exceeds finite viewport coordinates");
        }
        return false;
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    if (!runtime_enter(render_resource_context_for_world(player_world(s->player)), &scope, error, size)) {
        return false;
    }
    s->ui_dispatch_status = 0;
    uint64_t before = s->ui_dispatch_serial;
    unsigned capture = render_ui_input(ui, &value);
    bool dispatched_event = s->ui_dispatch_serial != before;
    bool wants_text = render_ui_wants_text(ui);
    av_value *record = trace_event(s, "target-ui-input");
    if (record) {
        av_obj_add_int(s->trace, record, "type", input->type);
        av_obj_add_float(s->trace, record, "ndc_x", input->x);
        av_obj_add_float(s->trace, record, "ndc_y", input->y);
        av_obj_add_float(s->trace, record, "wheel_x", input->wheel_x);
        av_obj_add_float(s->trace, record, "wheel_y", input->wheel_y);
        av_obj_add_int(s->trace, record, "button", input->button);
        av_obj_add_int(s->trace, record, "key", input->key);
        av_obj_add_uint(s->trace, record, "modifiers", input->modifiers);
        av_obj_add_uint(s->trace, record, "captured", capture);
        av_obj_add_bool(s->trace, record, "typing", wants_text);
        av_obj_add_bool(s->trace, record, "event_dispatched", dispatched_event);
        if (input->type == RML_INPUT_TEXT_INPUT) {
            av_obj_set(
                s->trace,
                record,
                "text",
                av_strn(s->trace, input->text, input->text_size)
            );
            s->trace_bytes += input->text_size;
        }
    }
    if (!runtime_leave(&scope, error, size)) {
        return false;
    }
    *captured = capture;
    *typing = wants_text;
    *dispatched = dispatched_event;
    if (s->ui_dispatch_status < 0) {
        if (size) {
            snprintf(error, size, "Target UI event failed (%d)", s->ui_dispatch_status);
        }
        return false;
    }
    return true;
}

const char *workspace_runtime_source_root(const workspace_runtime *s)
{
    return s->snapshot     ? workspace_snapshot_root(s->snapshot)
         : s->browser_root ? s->browser_root
                           : "";
}
workspace_runtime_result workspace_runtime_open_prepared(
    workspace_runtime *s,
    const char *path,
    const char *root,
    int document,
    int revision
)
{
    if (s->request) {
        return refuse(s, "busy", "A runtime candidate is already preparing");
    }
    if (s->next_session == INT_MAX) {
        return refuse(s, "limit", "Runtime session handles exhausted");
    }
    player_host host = s->config.host;
    host.detached_types = true;
    host.resources = render_prepared_resources_detached(root);
    char hash[65], *canonical = NULL;
    size_t loaded_bytes = 0;
    player *next = read_player(s, path, &host, hash, &loaded_bytes, &canonical);
    if (!next) {
        return refuse(s, "decode-failed", s->error);
    }
    adopt(s, next, document, revision, canonical, hash, loaded_bytes);
    free(canonical);
    s->error[0] = 0;
    snprintf(s->status, sizeof s->status, "ready");
    return result(s, true);
}

static bool reflected_field(const type_desc *type, const char *path, type_field *out)
{
    if (!path || !path[0] || strlen(path) > 256) {
        return false;
    }
    size_t offset = 0;
    for (int depth = 0; type && depth < TYPE_MAX_NESTING; depth++) {
        if (!type->field_count && type->alias_of) {
            type = type_catalog_find(type->alias_of);
            continue;
        }
        const char *end = strchr(path, '.');
        size_t n = end ? (size_t)(end - path) : strlen(path);
        const type_field *field = NULL;
        for (int i = 0; i < type->field_count; i++) {
            if (strlen(type->fields[i].name) == n &&
                !memcmp(type->fields[i].name, path, n)) {
                field = &type->fields[i];
                break;
            }
        }
        if (!field || field->offset > type->size ||
            field->size > type->size - field->offset) {
            return false;
        }
        if (!end) {
            *out = *field;
            out->offset += offset;
            return true;
        }
        if (field->type != PIN_STRUCT) {
            return false;
        }
        offset += field->offset;
        type = type_catalog_find(field->type_name);
        path = end + 1;
    }
    return false;
}
static bool floating_field(const type_desc *type, const char *path, type_field *out)
{
    return reflected_field(type, path, out) &&
           (out->type == PIN_F32 || out->type == PIN_F64);
}
bool workspace_runtime_value(
    workspace_runtime *s,
    int session,
    int object,
    const char *component,
    const char *field,
    char **json,
    char *error,
    size_t size
)
{
    if (json) {
        *json = NULL;
    }
    if (!s || !s->player || session != s->session || !json || !component ||
        strlen(component) > 256 || !field) {
        if (size) {
            snprintf(error, size, "Runtime value query is invalid or stale");
        }
        return false;
    }
    const scene_instance *scene = player_scene(s->player);
    if (object < 0 || object >= scene_object_count(scene_instance_scene(scene))) {
        if (size) {
            snprintf(error, size, "Runtime object is unavailable");
        }
        return false;
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    if (!runtime_enter(render_resource_context_for_world(player_world(s->player)), &scope, error, size)) {
        return false;
    }
    ecs_world *world = world_ecs(player_world(s->player));
    const ecs_component_desc *desc = NULL;
    for (int i = 0; i < ecs_world_component_count(world); i++) {
        const ecs_component_desc *row = ecs_world_component_at(world, i);
        if ((row->name && !strcmp(row->name, component)) ||
            (row->key && !strcmp(row->key, component))) {
            desc = row;
            break;
        }
    }
    const void *data = desc
                         ? ecs_component_data_const(world, scene_instance_entity(scene, object), desc->id)
                         : NULL;
    type_field selected;
    bool ok = data && reflected_field(desc->type, field, &selected);
    if (ok) {
        pin_value value;
        pin_read_field(data, &selected, &value);
        ok = value_to_json(&value, json, error, size);
    } else if (size) {
        snprintf(error, size, "Reflected runtime component or field is unavailable");
    }
    if (!runtime_leave(&scope, error, size)) {
        free(*json);
        *json = NULL;
        return false;
    }
    return ok;
}
bool workspace_runtime_number(
    workspace_runtime *s,
    int session,
    int object,
    const char *component,
    const char *field,
    bool write,
    double value,
    double *out,
    char *error,
    size_t size
)
{
    if ((write && s->config.read_only_presentation) || !s->player ||
        session != s->session || !component || !field || !out ||
        strlen(component) > 256 || !isfinite(value)) {
        if (size) {
            snprintf(error, size, "Runtime floating field query is invalid or stale");
        }
        return false;
    }
    const scene_instance *scene = player_scene(s->player);
    int total = scene_object_count(scene_instance_scene(scene));
    if (object < 0 || object >= total ||
        (write && engine_is_playing(world_engine(player_world(s->player))))) {
        if (size) {
            snprintf(error, size, "Runtime object is stale or simulation must be paused before editing");
        }
        return false;
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    if (!runtime_enter(render_resource_context_for_world(player_world(s->player)), &scope, error, size)) {
        return false;
    }
    ecs_world *world = world_ecs(player_world(s->player));
    const ecs_component_desc *desc = NULL;
    for (int i = 0; i < ecs_world_component_count(world); i++) {
        const ecs_component_desc *candidate = ecs_world_component_at(world, i);
        if ((candidate->name && !strcmp(candidate->name, component)) ||
            (candidate->key && !strcmp(candidate->key, component))) {
            desc = candidate;
            break;
        }
    }
    ecs_entity entity = scene_instance_entity(scene, object);
    type_field target;
    const void *data = desc ? ecs_component_data_const(world, entity, desc->id) : NULL;
    bool ok = data && floating_field(desc->type, field, &target);
    if (ok && write) {
        pin_value declared = { .type = target.type, .type_name = target.type_name };
        pin_value number = { .type = target.type, .type_name = target.type_name };
        if (target.range_contract) {
            ok = type_field_range_valid(&target);
            if (ok) {
                number = pin_field_number(&target, value);
            }
        } else if (target.type == PIN_F32) {
            ok = fabs(value) <= FLT_MAX;
            if (ok) {
                number.f = (float)value;
            }
        } else {
            number.d = value;
        }
        ok = ok &&
             (number.type == PIN_F32 ? isfinite(number.f) : isfinite(number.d)) &&
             ecs_component_write_value(world, entity, desc->id, target.offset, declared, number) ==
                 ECS_OK;
        if (ok) {
            av_value *record = trace_event(s, "field-write");
            if (record) {
                av_obj_add_int(s->trace, record, "object", object);
                av_obj_add_str(s->trace, record, "component", component);
                av_obj_add_str(s->trace, record, "field", field);
                av_obj_set(
                    s->trace,
                    record,
                    "value",
                    av_real(s->trace, number.type == PIN_F32 ? number.f : number.d)
                );
                s->trace_bytes += strlen(component) + strlen(field);
            }
        }
        data = ecs_component_data_const(world, entity, desc->id);
    }
    if (ok) {
        pin_value number;
        pin_read_field(data, &target, &number);
        *out = number.type == PIN_F32 ? number.f : number.d;
        ok = isfinite(*out);
    }
    if (!ok) {
        *out = 0;
    }
    if (!ok && size) {
        snprintf(error, size, "Runtime component or finite floating field is unavailable");
    }
    return runtime_leave(&scope, error, size) && ok;
}
bool workspace_runtime_subject(
    const workspace_runtime *s,
    int session,
    uint64_t owner,
    char subject[64],
    char *error,
    size_t size
)
{
    subject[0] = 0;
    if (!s || !s->player || session != s->session || !owner) {
        if (size) {
            snprintf(error, size, "Runtime subject session is unavailable or stale");
        }
        return false;
    }
    ecs_entity entity = ecs_entity_from_u64(owner);
    ecs_world *world = world_ecs(player_world(s->player));
    if (!ecs_entity_is_alive(world, entity) || ecs_entity_is_retiring(world, entity)) {
        if (size) {
            snprintf(error, size, "Runtime owner is unavailable or retired");
        }
        return false;
    }
    runtime_subject_token(entity, subject);
    return true;
}
bool workspace_runtime_object_subject(
    const workspace_runtime *s,
    int session,
    int object,
    char subject[64],
    char *error,
    size_t size
)
{
    subject[0] = 0;
    const scene_instance *instance = s && s->player && session == s->session
                                       ? player_scene(s->player)
                                       : NULL;
    if (!instance || object < 0 ||
        object >= scene_object_count(scene_instance_scene(instance))) {
        if (size) {
            snprintf(error, size, "Runtime object or session is unavailable or stale");
        }
        return false;
    }
    return workspace_runtime_subject(
        s,
        session,
        ecs_entity_to_u64(scene_instance_entity(instance, object)),
        subject,
        error,
        size
    );
}
static char *source_object_path(const scene_document *source, int object)
{
    int chain[SCENE_DEPTH_MAX + 1], count = 0;
    for (int i = object; i >= 0; i = source->entities[i].parent) {
        assert(count < SCENE_DEPTH_MAX + 1);
        chain[count++] = i;
    }
    size_t capacity = (size_t)count * 32 + 1;
    char *path = xmalloc(capacity);
    size_t used = 0;
    for (int i = count - 1; i >= 0; i--) {
        int index = chain[i], sibling = 0;
        for (int j = 0; j < index; j++) {
            sibling += source->entities[j].parent == source->entities[index].parent;
        }
        used += (size_t)snprintf(
            path + used,
            capacity - used,
            "/%s/%d",
            i == count - 1 ? "entities" : "children",
            sibling
        );
    }
    return path;
}
bool workspace_runtime_subject_origin(
    const workspace_runtime *s,
    int session,
    const char *subject,
    workspace_runtime_subject_info *out,
    char *error,
    size_t size
)
{
    *out = (workspace_runtime_subject_info){ .object = -1 };
    ecs_entity entity;
    const ecs_world *world = s && s->player ? world_ecs(player_world(s->player)) : NULL;
    if (!world || session != s->session ||
        !runtime_subject_resolve(world, subject, &entity)) {
        if (size) {
            snprintf(error, size, "Runtime subject or session is unavailable or stale");
        }
        return false;
    }
    out->prototype = xstrdup("");
    out->path = xstrdup("");
    out->source_path = xstrdup("");
    const scene_instance *instance = player_scene(s->player);
    const scene *scene = scene_instance_scene(instance);
    for (int i = 0; i < scene_object_count(scene); i++) {
        if (ecs_entity_to_u64(scene_instance_entity(instance, i)) !=
            ecs_entity_to_u64(entity)) {
            continue;
        }
        out->object = i;
        const scene_document *source = s->source_origins;
        if (scene != s->origin_scene || !source || i >= acount(source->entities) ||
            strcmp(scene_object_name(scene, i), source->entities[i].name) ||
            scene_object_parent(scene, i) != source->entities[i].parent) {
            return true;
        }
        free(out->prototype);
        free(out->path);
        out->prototype = xstrdup(source->entities[i].prototype);
        out->path = scene_document_entity_path(source, i);
        free(out->source_path);
        out->source_path = source_object_path(source, i);
        out->known = true;
        out->authored = !s->virtual_origins;
        return true;
    }
    entity_origin origin;
    if (workspace_runtime_origin(s, session, ecs_entity_to_u64(entity), &origin)) {
        out->object = origin.object;
        free(out->prototype);
        out->prototype = xstrdup(origin.prototype);
        out->creation = origin.creation;
        out->spawned = out->known = true;
    }
    return true;
}
bool workspace_runtime_origin(const workspace_runtime *s, int session, uint64_t owner, entity_origin *out)
{
    *out = (entity_origin){ 0 };
    if (!s || !s->player || session != s->session || !owner) {
        return false;
    }
    world *world = player_world(s->player);
    ecs_entity entity = ecs_entity_from_u64(owner);
    return ecs_entity_is_alive(world_ecs(world), entity) &&
           !ecs_entity_is_retiring(world_ecs(world), entity) &&
           entities_origin(world_entities(world), entity, out);
}
bool workspace_runtime_component(
    workspace_runtime *s,
    int session,
    int object,
    const char *component,
    bool *present,
    char *error,
    size_t size
)
{
    *present = false;
    const scene_instance *instance = s && s->player ? player_scene(s->player) : NULL;
    const scene *source = instance ? scene_instance_scene(instance) : NULL;
    if (!source || session != s->session || object < 0 ||
        object >= scene_object_count(source) || !component || strlen(component) > 256) {
        if (size) {
            snprintf(error, size, "Runtime component query is invalid or stale");
        }
        return false;
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    if (!runtime_enter(render_resource_context_for_world(player_world(s->player)), &scope, error, size)) {
        return false;
    }
    ecs_world *world = world_ecs(player_world(s->player));
    ecs_entity entity = scene_instance_entity(instance, object);
    if (!ecs_entity_is_alive(world, entity) || ecs_entity_is_retiring(world, entity) ||
        ecs_world_component_count(world) > 1024) {
        runtime_leave(&scope, error, size);
        if (size) {
            snprintf(error, size, "Runtime entity is retired or component inventory exceeds 1024");
        }
        return false;
    }
    for (int i = 0; i < ecs_world_component_count(world); i++) {
        const ecs_component_desc *desc = ecs_world_component_at(world, i);
        if ((desc->key && !strcmp(desc->key, component)) ||
            (desc->name && !strcmp(desc->name, component))) {
            *present = ecs_component_has(world, entity, desc->id);
            break;
        }
    }
    return runtime_leave(&scope, error, size);
}
bool workspace_runtime_subject_properties(
    workspace_runtime *s,
    int session,
    const char *subject,
    int first,
    int count,
    arena *storage,
    pin_array **rows,
    pin_array **choices,
    int *total,
    char **name,
    char *error,
    size_t size
)
{
    *name = NULL;
    ecs_entity entity = { 0 };
    ecs_world *world = s && s->player ? world_ecs(player_world(s->player)) : NULL;
    if (!s || !s->player || session != s->session ||
        !runtime_subject_resolve(world, subject, &entity)) {
        runtime_properties_query(NULL, entity, 0, 0, storage, rows, choices, total, error, size);
        if (size) {
            snprintf(error, size, "Runtime subject or session is unavailable or stale");
        }
        return false;
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    if (!runtime_enter(render_resource_context_for_world(player_world(s->player)), &scope, error, size)) {
        runtime_properties_query(NULL, entity, 0, 0, storage, rows, choices, total, error, 0);
        return false;
    }
    bool ok = runtime_properties_query(world, entity, first, count, storage, rows, choices, total, error, size);
    component_presentation presentation = { 0 };
    const ecs_component_desc **descriptors = NULL;
    for (int i = 0; i < ecs_world_component_count(world); i++) {
        apush(descriptors, ecs_world_component_at(world, i));
    }
    if (ok) {
        ok = workspace_presentation_load(
            s->config.documents,
            (ecs_component_catalog){ descriptors, acount(descriptors) },
            &presentation,
            error,
            size
        );
    }
    if (ok) {
        runtime_properties_present(world, *rows, storage, &presentation);
    } else {
        (*rows)->count = (*choices)->count = 0;
    }
    component_presentation_free(&presentation);
    afree(descriptors);

    const char *label = ecs_entity_name(world, entity);
    entity_origin origin;
    char fallback[512];
    if ((!label || !*label) &&
        workspace_runtime_origin(s, session, ecs_entity_to_u64(entity), &origin)) {
        snprintf(
            fallback,
            sizeof fallback,
            "%s #%" PRIu64,
            origin.prototype,
            origin.creation
        );
        label = fallback;
    }
    *name = arena_alloc(storage, strlen(label && *label ? label : subject) + 1);
    strcpy(*name, label && *label ? label : subject);
    return runtime_leave(&scope, error, size) && ok;
}
bool workspace_runtime_subject_write(
    workspace_runtime *s,
    int session,
    const char *subject,
    const char *component,
    const char *field,
    const char *json,
    char *error,
    size_t size
)
{
    ecs_entity entity = { 0 };
    ecs_world *world = s && s->player ? world_ecs(player_world(s->player)) : NULL;
    if (!s || !s->player || session != s->session || s->config.read_only_presentation ||
        !runtime_subject_resolve(world, subject, &entity)) {
        if (size) {
            snprintf(error, size, "Runtime edits require a current live subject and a writable session");
        }
        return false;
    }
    runtime_scope scope = { .components = player_declared_components(s->player) };
    if (!runtime_enter(render_resource_context_for_world(player_world(s->player)), &scope, error, size)) {
        return false;
    }
    bool ok = runtime_properties_write(world, entity, component, field, json, error, size);
    if (ok) {
        av_value *record = trace_event(s, "subject-field-write");
        size_t bytes = strlen(subject) + strlen(component) + strlen(field) +
                       strlen(json);
        if (record && bytes > 1024u * 1024u - s->trace_bytes) {
            s->trace_truncated = true;
        } else if (record) {
            av_obj_add_str(s->trace, record, "subject", subject);
            av_obj_add_str(s->trace, record, "component", component);
            av_obj_add_str(s->trace, record, "field", field);
            av_obj_add_str(s->trace, record, "json", json);
            s->trace_bytes += bytes;
        }
    }
    return runtime_leave(&scope, error, size) && ok;
}
static workspace_runtime_evidence_result evidence_result(workspace_runtime *s, bool ok)
{
    return (workspace_runtime_evidence_result){
        .ok = ok,
        .pending = !strcmp(s->evidence.state, "pending"),
        .job = s->evidence.handle,
        .state = s->evidence.state,
        .directory = s->evidence.directory,
        .error = s->evidence.error
    };
}
static workspace_runtime_evidence_result evidence_refuse(workspace_runtime *s, const char *error)
{
    /* A rejected request does not overwrite an existing pending job's state. */
    char message[512];
    snprintf(message, sizeof message, "%s", error);
    snprintf(s->error, sizeof s->error, "%s", message);
    return (workspace_runtime_evidence_result){ .ok = false,
                                                .pending = false,
                                                .job = 0,
                                                .state = "refused",
                                                .directory = "",
                                                .error = s->error };
}
workspace_runtime_evidence_result workspace_runtime_evidence_begin(
    workspace_runtime *s,
    int session,
    const char *directory
)
{
    if (!strcmp(s->evidence.state, "pending")) {
        return evidence_refuse(s, "A runtime capture is already pending");
    }
    if (session != s->session || !s->viewport || !s->drawn_current ||
        !engine_viewport_texture(s->viewport)) {
        return evidence_refuse(s, "Draw the current runtime viewport successfully before capturing it");
    }
    if (!directory || !directory[0] || strlen(directory) >= sizeof s->evidence.directory ||
        !player_relative_path(directory)) {
        return evidence_refuse(s, "Evidence destination must be a bounded workspace-relative directory");
    }
    if (s->evidence.next_handle == INT_MAX) {
        return evidence_refuse(s, "Runtime capture handle limit reached");
    }
    rooted_files *destination_root = rooted_files_open(s->root);
    char parent_path[1024];
    snprintf(parent_path, sizeof parent_path, "%s", directory);
    char *slash = strrchr(parent_path, '/');
    if (slash) {
        *slash = 0;
    } else {
        parent_path[0] = 0;
    }
    rooted_files *parent = destination_root
                             ? rooted_files_directory(destination_root, parent_path, false)
                             : NULL;
    rooted_file_info info;
    errno = 0;
    bool exists = parent && rooted_files_stat(destination_root, directory, &info);
    int disk_error = errno;
    bool valid_parent = parent != NULL;
    rooted_files_close(parent);
    rooted_files_close(destination_root);
    if (!valid_parent || exists || disk_error != ENOENT) {
        return evidence_refuse(s, exists ? "Evidence destination already exists" : "Evidence parent directory is missing or unsafe");
    }
    if (!workspace_runtime_verify_artifact(s, session, s->error, sizeof s->error)) {
        return evidence_refuse(s, s->error);
    }
    char *trace = av_json_write(av_doc_root(s->trace));
    workspace_runtime_evidence_input input = {
        .frozen_root = workspace_runtime_source_root(s),
        .artifact_relative = s->artifact_relative,
        .executed_artifact_sha256 = s->artifact_sha256,
        .executed_artifact_bytes = s->artifact_bytes,
        .scene = s->initial_scene,
        .engine_version = s->sdk_version,
        .build_id = s->sdk_build,
        .manifest_hash = s->sdk_manifest,
        .backend = s->drawn_backend,
        .video_driver = s->drawn_driver,
        .replay_trace_json = trace,
        .input_timeline_json = s->input_timeline,
        .session = s->session,
        .document = s->document,
        .revision = s->revision,
        .width = s->drawn_width,
        .height = s->drawn_height,
        .steps = s->drawn.steps,
        .random_state = s->drawn_random_state,
        .random_isolated = s->drawn_random_isolated,
        .host_time = s->now,
        .simulation_time = s->drawn.time,
        .camera = s->drawn_view,
        .perspective = s->drawn_perspective,
        .view_matrix = s->drawn_camera.view,
        .projection_matrix = s->drawn_camera.projection,
        .grid = s->grid,
        .unlit = s->unlit,
        .fogless = s->fogless,
        .colliders = s->show_colliders,
        .camera_frames = s->show_cameras,
        .debug_truncated = s->debug_geometry.truncated,
        .debug_unsupported = s->debug_geometry.unsupported,
        .playing = s->drawn.playing,
        .untracked = s->untracked,
        .trace_truncated = s->trace_truncated,
        .gpu_rendered = true
    };
    workspace_runtime_evidence_plan
        *plan = workspace_runtime_evidence_prepare(&input, s->error, sizeof s->error);
    free(trace);
    if (!plan) {
        return evidence_refuse(s, s->error);
    }
    CF_Canvas canvas = { .id = engine_viewport_take_canvas(s->viewport) };
    CF_Readback readback = canvas.id ? cf_canvas_readback(canvas)
                                     : (CF_Readback){ 0 };
    if (!readback.id) {
        if (canvas.id) {
            cf_destroy_canvas(canvas);
        }
        workspace_runtime_evidence_free(plan);
        s->drawn_current = false;
        return evidence_refuse(s, "Runtime viewport readback could not start");
    }
    evidence_release(s);
    s->evidence.plan = plan;
    s->evidence.canvas = canvas;
    s->evidence.readback = readback;
    s->evidence.handle = ++s->evidence.next_handle;
    s->evidence.width = s->drawn_width;
    s->evidence.height = s->drawn_height;
    s->evidence.started = cf_get_ticks();
    snprintf(s->evidence.directory, sizeof s->evidence.directory, "%s", directory);
    snprintf(s->evidence.state, sizeof s->evidence.state, "pending");
    s->evidence.error[0] = 0;
    s->drawn_current = false;
    s->texture_revision++;
    return evidence_result(s, true);
}
workspace_runtime_evidence_result workspace_runtime_evidence_poll(workspace_runtime *s, int job)
{
    if (job <= 0 || job != s->evidence.handle) {
        return evidence_refuse(s, "Runtime capture job is stale");
    }
    if (strcmp(s->evidence.state, "pending")) {
        return evidence_result(s, !strcmp(s->evidence.state, "complete"));
    }
    if (!cf_readback_ready(s->evidence.readback)) {
        if (cf_get_ticks() - s->evidence.started <= cf_get_tick_frequency() * 10) {
            return evidence_result(s, true);
        }
        snprintf(s->evidence.error, sizeof s->evidence.error, "Runtime viewport readback timed out");
        snprintf(s->evidence.state, sizeof s->evidence.state, "failed");
        evidence_release(s);
        return evidence_result(s, false);
    }
    /* Dimensions belong to the frozen plan, not the possibly replaced world. */
    int bytes = cf_readback_size(s->evidence.readback);
    bool ok = bytes > 0 && bytes <= WORKSPACE_FILE_BYTES_MAX;
    CF_Pixel *pixels = ok ? xmalloc((size_t)bytes) : NULL;
    ok = ok && cf_readback_data(s->evidence.readback, pixels, bytes) == bytes;
    void *png = NULL;
    int png_bytes = 0;
    /* Retain dimensions separately: subsequent draws may resize the viewport. */
    CF_Image image = { s->evidence.width, s->evidence.height, pixels };
    ok = ok &&
         bytes == s->evidence.width * s->evidence.height * (int)sizeof(CF_Pixel) &&
         !cf_is_error(cf_image_save_png_to_memory(&image, &png, &png_bytes)) &&
         png_bytes > 0;
    workspace_files *files = ok ? workspace_files_new(
                                      s->root,
                                      s->evidence.error,
                                      sizeof s->evidence.error
                                  )
                                : NULL;
    ok = files && workspace_runtime_evidence_finalize(
                      s->evidence.plan,
                      files,
                      s->evidence.directory,
                      png,
                      (size_t)png_bytes,
                      s->evidence.error,
                      sizeof s->evidence.error
                  );
    workspace_files_free(files);
    cf_free(png);
    free(pixels);
    if (!ok && !s->evidence.error[0]) {
        snprintf(s->evidence.error, sizeof s->evidence.error, "Runtime viewport PNG readback or encoding failed");
    }
    snprintf(s->evidence.state, sizeof s->evidence.state, "%s", ok ? "complete" : "failed");
    evidence_release(s);
    return evidence_result(s, ok);
}
workspace_runtime_evidence_result workspace_runtime_evidence_cancel(workspace_runtime *s, int job)
{
    if (job <= 0 || job != s->evidence.handle) {
        return evidence_refuse(s, "Runtime capture job is stale");
    }
    if (!strcmp(s->evidence.state, "pending")) {
        evidence_release(s);
        snprintf(s->evidence.state, sizeof s->evidence.state, "cancelled");
    }
    return evidence_result(s, true);
}

save *workspace_runtime_save(workspace_runtime *s, int session, bool write, char *error, size_t size)
{
    if (!s || !s->player || session != s->session ||
        (write && s->config.read_only_presentation)) {
        if (size) {
            snprintf(error, size, "Runtime saved-data session is stale or read-only");
        }
        return NULL;
    }
    return world_modules(player_world(s->player))->save;
}

void workspace_runtime_model_present(
    workspace_runtime *s,
    int object,
    int part,
    bool section,
    const float normal[3],
    float offset
)
{
    s->model_view.scene = scene_instance_scene(player_scene(s->player));
    s->model_view.object = object;
    s->model_view.part = part;
    s->model_view.section = section;
    memcpy(s->model_view.normal, normal, sizeof s->model_view.normal);
    s->model_view.offset = offset;
}
bool workspace_runtime_trace(workspace_runtime *s, int session, char **json, char *error, size_t size)
{
    *json = NULL;
    if (!s || !s->player || session != s->session || !s->trace) {
        if (size) {
            snprintf(error, size, "Runtime trace session is unavailable or stale");
        }
        return false;
    }
    *json = av_json_write(av_doc_root(s->trace));
    return *json != NULL;
}
