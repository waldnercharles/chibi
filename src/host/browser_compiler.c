#include "host/browser_compiler.h"

#include "authoring/workspace_documents.h"
#include "host/workspace_files_web.h"
#include "host/workspace_overlays.h"
#include "support/rooted_files.h"
#include "support/utf8.h"
#include "support/xalloc.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// clang-format off
EM_JS(int, worker_available, (), {
    return !!(Module.browserCompiler && Module.browserCompiler.available());
});
EM_JS(int, worker_start, (int id, const char *root, const char *overlays, const char *scene, int authoring), {
    return Module.browserCompiler && Module.browserCompiler.start(
                                         id,
                                         UTF8ToString(root),
                                         UTF8ToString(overlays),
                                         UTF8ToString(scene),
                                         !!authoring
                                     )
             ? 1
             : 0;
});
EM_JS(void, worker_cancel, (int id), {
    if (Module.browserCompiler) {
        Module.browserCompiler.cancel(id);
    }
});
EM_JS(void, worker_destroy, (int id), {
    if (Module.browserCompiler) {
        Module.browserCompiler.destroy(id);
    }
});
EM_JS(void, worker_detach, (int id), {
    if (Module.browserCompiler) {
        Module.browserCompiler.detach(id);
    }
});
EM_JS(void, worker_remove, (const char *root), {
    if (Module.browserCompiler) {
        Module.browserCompiler.remove(UTF8ToString(root));
    }
});
// clang-format on
#else
static int worker_available()
{
    return 0;
}
static int worker_start(int id, const char *root, const char *overlays, const char *scene, int authoring)
{
    (void)id;
    (void)root;
    (void)overlays;
    (void)scene;
    (void)authoring;
    return 0;
}
static void worker_cancel(int id)
{
    (void)id;
}
static void worker_destroy(int id)
{
    (void)id;
}
static void worker_detach(int id)
{
    (void)id;
}
static void worker_remove(const char *root)
{
    (void)root;
}
#endif
#define COMPILERS_MAX 8
#define ROOT_MAX 4096
#define LOG_MAX 32768
struct browser_compiler
{
    char *workspace, *root, *artifact;
    workspace_documents *documents;
    int instance;
    browser_compiler_state state;
    char log[LOG_MAX + 1], error[512];
};
static browser_compiler *instances[COMPILERS_MAX];
static int next_instance;
static bool fail(char *error, size_t size, const char *text)
{
    if (size) {
        snprintf(error, size, "%s", text);
    }
    return false;
}
browser_compiler *browser_compiler_new(
    const char *root,
    workspace_documents *documents,
    char *error,
    size_t size
)
{
    if (!root || root[0] != '/' || strlen(root) > ROOT_MAX || !documents) {
        fail(error, size, "Browser compiler requires a rooted workspace and document service");
        return NULL;
    }
    int slot = 0;
    while (slot < COMPILERS_MAX && instances[slot]) {
        slot++;
    }
    if (slot == COMPILERS_MAX) {
        fail(error, size, "Browser compiler service limit exceeded");
        return NULL;
    }
    workspace_files_web_init();
    browser_compiler *s = xcalloc(1, sizeof *s);
    s->workspace = xstrdup(root);
    s->documents = documents;
    instances[slot] = s;
    return s;
}
bool browser_compiler_available(const browser_compiler *s)
{
    return s && worker_available();
}
void browser_compiler_snapshot_free(char *root)
{
    if (!root) {
        return;
    }
    worker_remove(root);
    free(root);
}
static void clear_result(browser_compiler *s)
{
    if (s->instance) {
        worker_destroy(s->instance);
    }
    s->instance = 0;
    /* The worker owns untransferred root deletion; take_root detached it. */
    free(s->root);
    s->root = NULL;
    free(s->artifact);
    s->artifact = NULL;
    s->log[0] = s->error[0] = 0;
}
void browser_compiler_cancel(browser_compiler *s)
{
    if (!s || s->state != BROWSER_COMPILER_RUNNING) {
        return;
    }
    s->state = BROWSER_COMPILER_CANCELLED;
    worker_cancel(s->instance);
    snprintf(s->error, sizeof s->error, "Browser compilation cancelled");
}
void browser_compiler_free(browser_compiler *s)
{
    if (!s) {
        return;
    }
    browser_compiler_cancel(s);
    clear_result(s);
    for (int i = 0; i < COMPILERS_MAX; i++) {
        if (instances[i] == s) {
            instances[i] = NULL;
        }
    }
    free(s->workspace);
    free(s);
}
static bool relative_scene(const char *path)
{
    if (!path || path[0] == '/' || strchr(path, '\\') || strchr(path, ':')) {
        return false;
    }
    if (!path[0]) {
        return true;
    }
    for (const char *p = path; *p;) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (!n || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) {
            return false;
        }
        if (!end) {
            return true;
        }
        p = end + 1;
    }
    return false;
}
bool browser_compiler_start_sources(
    browser_compiler *s,
    const char *scene,
    const char *const *paths,
    const char *const *sources,
    int count,
    bool authoring,
    char *error,
    size_t size
)
{
    if (!s || s->state == BROWSER_COMPILER_RUNNING) {
        return fail(error, size, "Browser compiler is busy");
    }
    if (!browser_compiler_available(s)) {
        return fail(error, size, "Browser compiler worker is unavailable");
    }
    if (!scene || strlen(scene) > 1024 || !relative_scene(scene)) {
        return fail(error, size, "Browser scene path exceeds 1024 bytes");
    }
    if (next_instance == INT_MAX) {
        return fail(error, size, "Browser compilation request identities exhausted");
    }
    if (count < 0 || count > 16 || (count && (!paths || !sources))) {
        return fail(error, size, "Private source set exceeds 16 rows");
    }
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        if (!paths[i] || !sources[i] || !rooted_files_path_valid(paths[i]) ||
            strlen(paths[i]) > 1024) {
            return fail(error, size, "Private source path is invalid");
        }
        size_t bytes = strlen(sources[i]);
        if (bytes > 1024u * 1024u || !utf8_text_valid(sources[i], bytes) ||
            bytes > 4u * 1024u * 1024u - total) {
            return fail(error, size, "Private source set exceeds bounded UTF-8 limits");
        }
        total += bytes;
        for (int j = 0; j < i; j++) {
            size_t a = strlen(paths[i]), b = strlen(paths[j]);
            if (!strcmp(paths[i], paths[j]) ||
                (a < b && !strncmp(paths[i], paths[j], a) && paths[j][a] == '/') ||
                (b < a && !strncmp(paths[i], paths[j], b) && paths[i][b] == '/')) {
                return fail(error, size, "Private source paths must be unique files without directory conflicts");
            }
        }
    }
    char *overlays = workspace_overlays_json(s->documents, error, size);
    if (!overlays) {
        return false;
    }
    if (count) {
        yyjson_doc *original = yyjson_read(overlays, strlen(overlays), 0);
        yyjson_val *rows = original ? yyjson_doc_get_root(original) : NULL;
        bool valid = yyjson_is_arr(rows);
        rooted_files *root = rooted_files_open(s->workspace);
        for (int i = 0; valid && i < count; i++) {
            rooted_file_info info;
            errno = 0;
            bool exists = root && rooted_files_stat(root, paths[i], &info);
            valid = root && !exists && errno == ENOENT;
            size_t index, max;
            yyjson_val *row;
            yyjson_arr_foreach(rows, index, max, row)
            {
                yyjson_val *path = yyjson_obj_get(row, "path");
                if (!yyjson_is_str(path) || !strcmp(yyjson_get_str(path), paths[i])) {
                    valid = false;
                }
            }
        }
        rooted_files_close(root);
        yyjson_mut_doc *copy = valid ? yyjson_doc_mut_copy(original, NULL) : NULL;
        for (int i = 0; copy && i < count; i++) {
            yyjson_mut_val *extra = yyjson_mut_arr_add_obj(copy, yyjson_mut_doc_get_root(copy));
            yyjson_mut_obj_add_strcpy(copy, extra, "path", paths[i]);
            yyjson_mut_obj_add_strcpy(copy, extra, "text", sources[i]);
        }
        char *expanded = copy ? yyjson_mut_write(copy, 0, NULL) : NULL;
        yyjson_mut_doc_free(copy);
        yyjson_doc_free(original);
        free(overlays);
        if (!expanded) {
            return fail(error, size, "Private sources conflict with existing or open workspace paths");
        }
        overlays = expanded;
    }
    clear_result(s);
    s->instance = ++next_instance;
    s->state = BROWSER_COMPILER_RUNNING;
    bool accepted = worker_start(s->instance, s->workspace, overlays, scene, authoring) !=
                    0;
    free(overlays);
    if (!accepted) {
        s->state = BROWSER_COMPILER_FAILED;
        snprintf(s->error, sizeof s->error, "Browser worker refused the compilation request");
        return fail(error, size, s->error);
    }
    return true;
}
bool browser_compiler_start(browser_compiler *s, const char *scene, bool authoring, char *error, size_t size)
{
    return browser_compiler_start_sources(s, scene, NULL, NULL, 0, authoring, error, size);
}
bool browser_compiler_start_scene_source(
    browser_compiler *s,
    const char *scene,
    const char *source,
    bool authoring,
    char *error,
    size_t size
)
{
    if (!source) {
        return fail(error, size, "Virtual scene source is required");
    }
    const char *paths[] = { scene }, *sources[] = { source };
    return browser_compiler_start_sources(s, scene, paths, sources, 1, authoring, error, size);
}
browser_compiler_result browser_compiler_poll(const browser_compiler *s)
{
    if (!s) {
        return (browser_compiler_result){
            .state = BROWSER_COMPILER_FAILED,
            .root = "",
            .artifact = "",
            .log = "",
            .error = "Browser compiler service is unavailable"
        };
    }
    return (browser_compiler_result){ s->state,
                                      s->root ? s->root : "",
                                      s->artifact ? s->artifact : "",
                                      s->log,
                                      s->error };
}
char *browser_compiler_take_root(browser_compiler *s)
{
    if (!s || s->state != BROWSER_COMPILER_SUCCEEDED || !s->root) {
        return NULL;
    }
    worker_detach(s->instance);
    char *root = s->root;
    s->root = NULL;
    return root;
}
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
bool browser_compiler_complete(
    int id,
    int state,
    const char *root,
    const char *artifact,
    const char *log,
    const char *error
)
{
    browser_compiler *s = NULL;
    for (int i = 0; i < COMPILERS_MAX; i++) {
        if (instances[i] && instances[i]->instance == id) {
            s = instances[i];
        }
    }
    if (!s || s->state != BROWSER_COMPILER_RUNNING ||
        state < BROWSER_COMPILER_SUCCEEDED || state > BROWSER_COMPILER_CANCELLED) {
        return false;
    }
    snprintf(s->log, sizeof s->log, "%s", log ? log : "");
    snprintf(s->error, sizeof s->error, "%s", error ? error : "");
    if (state == BROWSER_COMPILER_SUCCEEDED) {
        size_t n = root ? strnlen(root, ROOT_MAX + 1) : 0;
        if (!n || n > ROOT_MAX || root[0] != '/' || !artifact ||
            strlen(artifact) > ROOT_MAX || strncmp(root, artifact, n) ||
            artifact[n] != '/') {
            s->state = BROWSER_COMPILER_FAILED;
            snprintf(s->error, sizeof s->error, "Browser worker returned an invalid frozen artifact root");
            return false;
        }
        s->root = xstrdup(root);
        s->artifact = xstrdup(artifact);
    }
    s->state = (browser_compiler_state)state;
    return true;
}
