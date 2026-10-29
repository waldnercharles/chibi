#include "toolchain/compiler_bootstrap.h"

#include "execution/status.h"
#include "support/asset_text.h"
#include "support/asset_value.h"
#include "support/sha256.h"
#include "support/xalloc.h"
#include "toolchain/compiler_host.h"

#include <assert.h>
#include <dirent.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yyjson.h>

/* The manifest format this host reads and writes. */
#define BOOTSTRAP_FORMAT 1
/* Instructions one compile of the compiler sources may run (fits 32-bit uintptr_t). */
#define BOOTSTRAP_BUDGET 4000000000u
#define CLOSURE_FILE "compiler.chibip"
#define MANIFEST_FILE "bootstrap.json"
#define CONTRACT_FILE "bootstrap.kdl"
/* Below the compiler sources: the compiler's sources in Compile's order. */
#define SOURCES_FILE "asset_compiler/CompilerSources.txt"
/* Compile's arguments: the project, its collections' names and paths, the
 * source paths and texts, whether to emit debugger points and watches, which
 * the compiler is compiled without, then the six that describe the target:
 * Target, prepared ABI, instruction names, natives, ports and native
 * identities. */
#define ARGUMENTS 12
#define DEBUG_ARGUMENT 5
#define TARGET_FIRST 6

static char *join(const char *base, const char *name)
{
    size_t size = strlen(base) + strlen(name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", base, name);
    return path;
}

/* A regular file's bytes, NUL-terminated, if it is at most limit long. */
static unsigned char *read_bytes(const char *path, size_t limit, size_t *length)
{
    struct stat info;
    bool fits = !stat(path, &info) && S_ISREG(info.st_mode) &&
                (uintmax_t)info.st_size <= limit;
    FILE *file = fits ? fopen(path, "rb") : NULL;
    if (!file) {
        return NULL;
    }
    *length = (size_t)info.st_size;
    unsigned char *bytes = xmalloc(*length + 1);
    bool read = fread(bytes, 1, *length, file) == *length && fgetc(file) == EOF;
    fclose(file);
    if (!read) {
        free(bytes);
        return NULL;
    }
    bytes[*length] = 0;
    return bytes;
}

/* Replaces folder/name with bytes through a new temporary file beside it,
 * readable as umask allows. Rename is the commit point. */
static bool write_bytes(const char *folder, const char *name, const void *bytes, size_t length)
{
    char *path = join(folder, name), *temporary = join(folder, ".partial-XXXXXX");
    mode_t mask = umask(0);
    umask(mask);
    int fd = mkstemp(temporary);
    FILE *file = fd >= 0 && !fchmod(fd, 0666 & ~mask) ? fdopen(fd, "wb") : NULL;
    if (fd >= 0 && !file) {
        close(fd);
    }
    bool written = file && fwrite(bytes, 1, length, file) == length;
    if (file) {
        written = !fclose(file) && written;
    }
    written = written && !rename(temporary, path);
    if (!written && fd >= 0) {
        remove(temporary);
    }
    free(temporary);
    free(path);
    return written;
}

/* Replaces the file at path with bytes, as write_bytes does. */
static bool write_path(const char *path, const void *bytes, size_t length)
{
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
#endif
    if (!slash) {
        return write_bytes(".", path, bytes, length);
    }
    size_t size = (size_t)(slash - path);
    char *folder = xmalloc(size + 2);
    memcpy(folder, path, size);
    folder[size ? size : 1] = 0;
    if (!size) {
        folder[0] = '/';
    }
    bool written = write_bytes(folder, slash + 1, bytes, length);
    free(folder);
    return written;
}

/* Makes the folder unless it already is one. */
static bool make_folder(const char *path)
{
    struct stat info;
    return !mkdir(path, 0777) || (!stat(path, &info) && S_ISDIR(info.st_mode));
}

static bool same(const unsigned char *a, size_t a_length, const unsigned char *b, size_t b_length)
{
    return a_length == b_length && !memcmp(a, b, a_length);
}

/* The sources' bootstrap contract: their bootstrap version and the oldest
 * version that compiles them. */
typedef struct
{
    int version, minimum;
} contract;

static bool read_contract(const char *compiler, contract *c, char *error, size_t size)
{
    char *path = join(compiler, CONTRACT_FILE);
    size_t length;
    char *text = (char *)read_bytes(path, 4096, &length);
    char reason[256] = "it cannot be read";
    av_doc *doc = text ? av_doc_read(text, reason, sizeof reason) : NULL;
    av_value *root = doc ? av_doc_root(doc) : NULL;
    *c = (contract){ .version = root ? av_get_int(av_obj_get(root, "version")) : 0,
                     .minimum = root ? av_get_int(av_obj_get(root, "minimum")) : 0 };
    bool valid = c->minimum >= 1 && c->minimum <= c->version;
    if (!valid) {
        snprintf(error, size, "%s: %s", path, doc ? "needs a version and a minimum from 1 to it" : reason);
    }
    av_doc_free(doc);
    free(text);
    free(path);
    return valid;
}

/* The ID of a callable of the compiler's root package. */
static uint64_t entry(const char *name)
{
    char symbol[96];
    snprintf(symbol, sizeof symbol, "project:package_compiler:1:%s:callable", name);
    return compiler_host_entry(symbol);
}

/* Whether the six target values from first in values are this host's, as
 * compiler_host_target_json describes it. */
static bool target_matches(const compiler_host *host, yyjson_val *values, size_t first, char *error, size_t size)
{
    static const char *const PARTS[] = {
        "value layout", "prepared ABI", "instruction names",
        "natives",      "native ports", "native identities"
    };
    size_t length;
    char *json = compiler_host_target_json(host, &length);
    yyjson_doc *doc = yyjson_read(json, length, 0);
    yyjson_val *own = yyjson_doc_get_root(doc);
    bool valid = true;
    for (size_t i = 0; valid && i < sizeof PARTS / sizeof *PARTS; i++) {
        valid = yyjson_equals(yyjson_arr_get(values, first + i), yyjson_arr_get(own, i));
        if (!valid) {
            snprintf(error, size, "the artifact was compiled for other %s than this host's", PARTS[i]);
        }
    }
    yyjson_doc_free(doc);
    free(json);
    return valid;
}

/* Where a recorded source is on disk: below the core collection when its path
 * is under core_path, and otherwise below the compiler sources. NULL for a
 * path that would leave them. */
static char *disk_path(const compiler_bootstrap *b, const char *core_path, const char *path)
{
    size_t length = strlen(core_path);
    bool core = !strncmp(path, core_path, length) && path[length] == '/';
    const char *rest = core ? path + length + 1 : path;
    if (*rest == '/' || !strncmp(rest, "../", 3) || strstr(rest, "/../")) {
        return NULL;
    }
    return join(core ? b->core : b->compiler, rest);
}

/* Runs CompileClosure on arguments: the compiler's closure, or NULL with why. */
static unsigned char *compile(
    compiler_host *host,
    const char *json,
    size_t length,
    size_t *size,
    char *error,
    size_t error_size
)
{
    compiler_host_bytes out = { 0 };
    bool called = compiler_host_invoke_bytes(
        host,
        entry("CompileClosure"),
        json,
        length,
        BOOTSTRAP_BUDGET,
        COMPILER_HOST_MAX_ARTIFACT_BYTES,
        &out
    );
    if (called && out.flow_status == FLOW_OK && out.length) {
        *size = out.length;
        return out.bytes;
    }
    free(out.bytes);
    if (!called) {
        snprintf(error, error_size, "the compiler failed: %s", out.diagnostic);
        return NULL;
    }
    /* Compile says why it rejected the sources. */
    compiler_host_result rejected = { 0 };
    bool explained = compiler_host_invoke(host, entry("Compile"), json, length, BOOTSTRAP_BUDGET, &rejected);
    snprintf(
        error,
        error_size,
        "the compiler rejected the sources: %.200s",
        explained ? rejected.json : rejected.diagnostic
    );
    free(rejected.json);
    return NULL;
}

/* A seed: a closure and Compile's arguments for the sources it compiled. */
typedef struct
{
    unsigned char *closure;
    size_t closure_length;
    char *json;
    size_t json_length;
    yyjson_doc *doc;
    yyjson_val *args;
} seed;

static void free_seed(seed *s)
{
    yyjson_doc_free(s->doc);
    free(s->json);
    free(s->closure);
}

/* Reads the seed folder; false with why. Its arguments name one project, the
 * core collection, matched source paths and texts, no debugger points and
 * watches, and a target. */
static bool read_seed(const char *folder, seed *s, char *error, size_t size)
{
    char *closure = join(folder, CLOSURE_FILE), *json = join(folder, "compile.json");
    s->closure = read_bytes(closure, COMPILER_HOST_MAX_ARTIFACT_BYTES, &s->closure_length);
    s->json = (char *)read_bytes(json, COMPILER_HOST_MAX_BYTES, &s->json_length);
    s->doc = s->json
               ? yyjson_read(s->json, s->json_length, YYJSON_READ_ALLOW_INVALID_UNICODE)
               : NULL;
    s->args = yyjson_doc_get_root(s->doc);
    free(json);
    free(closure);
    yyjson_val *names = yyjson_arr_get(s->args, 1),
               *paths = yyjson_arr_get(s->args, 2);
    yyjson_val *files = yyjson_arr_get(s->args, 3),
               *texts = yyjson_arr_get(s->args, 4);
    bool valid = s->closure && yyjson_is_arr(s->args) &&
                 yyjson_arr_size(s->args) == ARGUMENTS &&
                 yyjson_is_str(yyjson_arr_get(s->args, 0)) &&
                 yyjson_arr_size(names) == 1 &&
                 yyjson_equals_str(yyjson_arr_get(names, 0), "core") &&
                 yyjson_arr_size(paths) == 1 &&
                 yyjson_is_str(yyjson_arr_get(paths, 0)) &&
                 yyjson_arr_size(files) > 0 &&
                 yyjson_arr_size(texts) == yyjson_arr_size(files) &&
                 yyjson_is_false(yyjson_arr_get(s->args, DEBUG_ARGUMENT));
    if (!valid) {
        snprintf(error, size, "%s needs %s and compile.json, Compile's arguments", folder, CLOSURE_FILE);
    }
    return valid;
}

/* A list of compiler source paths: Compile's source order. */
typedef struct
{
    char **paths;
    size_t count;
} source_list;

static void free_list(source_list *list)
{
    for (size_t i = 0; i < list->count; i++) {
        free(list->paths[i]);
    }
    free(list->paths);
    *list = (source_list){ 0 };
}

static void add_path(source_list *list, const char *path, size_t length)
{
    list->paths = xrealloc(list->paths, (list->count + 1) * sizeof *list->paths);
    list->paths[list->count] = xmalloc(length + 1);
    memcpy(list->paths[list->count], path, length);
    list->paths[list->count++][length] = 0;
}

/* The paths a JSON array of strings holds, or of objects with a path. */
static source_list json_list(yyjson_val *sources)
{
    source_list list = { 0 };
    for (size_t i = 0; i < yyjson_arr_size(sources); i++) {
        yyjson_val *source = yyjson_arr_get(sources, i);
        yyjson_val *path = yyjson_is_obj(source) ? yyjson_obj_get(source, "path")
                                                 : source;
        const char *text = yyjson_get_str(path);
        add_path(&list, text ? text : "", text ? yyjson_get_len(path) : 0);
    }
    return list;
}

/* The compiler sources the folder lists, one path to a line, in the order
 * Compile takes them. False with why when the list is missing or empty. */
static bool read_sources(const compiler_bootstrap *b, source_list *list, char *error, size_t size)
{
    char *file = join(b->compiler, SOURCES_FILE);
    size_t length;
    char *text = (char *)read_bytes(file, COMPILER_HOST_MAX_BYTES, &length);
    for (char *line = text, *next; line && *line; line = next) {
        char *end = strchr(line, '\n');
        next = end ? end + 1 : line + strlen(line);
        size_t size_of_line = (size_t)((end ? end : next) - line);
        if (size_of_line && line[size_of_line - 1] == '\r') {
            size_of_line--;
        }
        if (size_of_line) {
            add_path(list, line, size_of_line);
        }
    }
    if (!list->count) {
        snprintf(error, size, "%s lists no compiler sources", file);
    }
    free(text);
    free(file);
    return list->count > 0;
}

static bool recorded(const source_list *sources, const char *path)
{
    for (size_t i = 0; i < sources->count; i++) {
        if (!strcmp(sources->paths[i], path)) {
            return true;
        }
    }
    return false;
}

/* Whether each .chibi file in the folder of each listed source, a package,
 * is itself listed: a package that gained a file the list does not name would
 * compile without it. False with the first file that is not. */
static bool packages_complete(
    const compiler_bootstrap *b,
    const char *core_path,
    const source_list *sources,
    char *error,
    size_t size
)
{
    bool valid = true;
    for (size_t i = 0; valid && i < sources->count; i++) {
        const char *path = sources->paths[i];
        char *file = disk_path(b, core_path, path);
        if (!file) {
            continue;
        }
        char *last = strrchr(file, '/');
        if (!last) {
            free(file);
            continue;
        }
        *last = 0;
        const char *slash = strrchr(path, '/');
        int prefix = slash ? (int)(slash - path) + 1 : 0;
        DIR *dir = opendir(file);
        for (struct dirent *entry; valid && dir && (entry = readdir(dir));) {
            size_t length = strlen(entry->d_name);
            if (entry->d_name[0] == '.' || length < 7 ||
                strcmp(entry->d_name + length - 6, ".chibi")) {
                continue;
            }
            char sibling[4096];
            snprintf(sibling, sizeof sibling, "%.*s%s", prefix, path, entry->d_name);
            valid = recorded(sources, sibling);
            if (!valid) {
                snprintf(
                    error,
                    size,
                    "%s is in the package of a compiler source but %s "
                    "does not list it",
                    sibling,
                    SOURCES_FILE
                );
            }
        }
        if (dir) {
            closedir(dir);
        }
        free(file);
    }
    return valid;
}

/* Records each source the seed compiled, with its checksum, if it is what is
 * on disk; NULL with why for the first that is not. */
static yyjson_mut_val *record_sources(
    const compiler_bootstrap *b,
    const seed *s,
    yyjson_mut_doc *doc,
    char *error,
    size_t size
)
{
    yyjson_mut_val *sources = yyjson_mut_arr(doc);
    const char *core_path = yyjson_get_str(yyjson_arr_get(yyjson_arr_get(s->args, 2), 0));
    yyjson_val *paths = yyjson_arr_get(s->args, 3),
               *texts = yyjson_arr_get(s->args, 4);
    for (size_t i = 0; i < yyjson_arr_size(paths); i++) {
        const char *path = yyjson_get_str(yyjson_arr_get(paths, i));
        yyjson_val *text = yyjson_arr_get(texts, i);
        char *file = path ? disk_path(b, core_path, path) : NULL;
        size_t length = 0;
        unsigned char *disk = file ? read_bytes(file, COMPILER_HOST_MAX_BYTES, &length)
                                   : NULL;
        bool valid = disk && yyjson_is_str(text) &&
                     same(disk, length, (const unsigned char *)yyjson_get_str(text), yyjson_get_len(text));
        char digest[65];
        if (valid) {
            sha256_hex(disk, length, digest);
            yyjson_mut_val *source = yyjson_mut_arr_add_obj(doc, sources);
            yyjson_mut_obj_add_strcpy(doc, source, "path", path);
            yyjson_mut_obj_add_uint(doc, source, "bytes", length);
            yyjson_mut_obj_add_strcpy(doc, source, "sha256", digest);
        }
        free(disk);
        free(file);
        if (!valid) {
            snprintf(error, size, "the seed compiled %s other than it is on disk", path ? path : "a source");
            return NULL;
        }
    }
    source_list listed = json_list(paths);
    bool complete = packages_complete(b, core_path, &listed, error, size);
    free_list(&listed);
    return complete ? sources : NULL;
}

/* The manifest of the seed's closure, its sources and its target. */
static yyjson_mut_doc *manifest(
    const compiler_bootstrap *b,
    const seed *s,
    const contract *c,
    char *error,
    size_t size
)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *sources = record_sources(b, s, doc, error, size);
    if (!sources) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    char digest[65], id[19];
    sha256_hex(s->closure, s->closure_length, digest);
    snprintf(id, sizeof id, "0x%016" PRIx64, entry("CompileClosure"));
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "format", BOOTSTRAP_FORMAT);
    yyjson_mut_obj_add_int(doc, root, "version", c->version);
    yyjson_mut_obj_add_int(doc, root, "minimum", c->minimum);
    yyjson_mut_obj_add_strcpy(doc, root, "revision", b->revision ? b->revision : "");
    yyjson_mut_obj_add_strcpy(doc, root, "entry", id);
    yyjson_mut_val *closure = yyjson_mut_obj_add_obj(doc, root, "closure");
    yyjson_mut_obj_add_str(doc, closure, "file", CLOSURE_FILE);
    yyjson_mut_obj_add_uint(doc, closure, "bytes", s->closure_length);
    yyjson_mut_obj_add_strcpy(doc, closure, "sha256", digest);
    yyjson_mut_obj_add_strcpy(doc, root, "project", yyjson_get_str(yyjson_arr_get(s->args, 0)));
    yyjson_mut_obj_add_strcpy(
        doc,
        root,
        "core",
        yyjson_get_str(yyjson_arr_get(yyjson_arr_get(s->args, 2), 0))
    );
    yyjson_mut_obj_add_val(doc, root, "sources", sources);
    yyjson_mut_val *target = yyjson_mut_obj_add_arr(doc, root, "target");
    for (size_t i = TARGET_FIRST; i < ARGUMENTS; i++) {
        yyjson_mut_arr_append(target, yyjson_val_mut_copy(doc, yyjson_arr_get(s->args, i)));
    }
    return doc;
}

bool compiler_bootstrap_write(const compiler_bootstrap *b, char *error, size_t size)
{
    contract c;
    seed s = { 0 };
    bool valid = read_contract(b->compiler, &c, error, size) &&
                 read_seed(b->seed, &s, error, size);
    yyjson_mut_doc *doc = valid ? manifest(b, &s, &c, error, size) : NULL;
    compiler_host *host = doc ? compiler_host_load(
                                    s.closure,
                                    s.closure_length,
                                    behavior_default_backend(),
                                    error,
                                    size
                                )
                              : NULL;
    size_t length = 0;
    unsigned char *output = host && target_matches(host, s.args, TARGET_FIRST, error, size)
                              ? compile(host, s.json, s.json_length, &length, error, size)
                              : NULL;
    valid = output && same(output, length, s.closure, s.closure_length);
    if (output && !valid) {
        snprintf(error, size, "the seed compiles the sources to %zu other bytes, not to itself", length);
    }
    char *text = valid
                   ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_ALLOW_INVALID_UNICODE, &length)
                   : NULL;
    if (valid) {
        valid = text && make_folder(b->folder) &&
                write_bytes(b->folder, CLOSURE_FILE, s.closure, s.closure_length) &&
                write_bytes(b->folder, MANIFEST_FILE, text, length);
        if (!valid) {
            snprintf(error, size, "cannot write the artifact into %s", b->folder);
        }
    }
    free(text);
    free(output);
    yyjson_mut_doc_free(doc);
    compiler_host_free(host);
    free_seed(&s);
    return valid;
}

/* An artifact this host reads: its manifest and its closure. */
typedef struct
{
    unsigned char *closure;
    size_t closure_length;
    char *text;
    yyjson_doc *doc;
    yyjson_val *root;
} artifact;

static void free_artifact(artifact *a)
{
    yyjson_doc_free(a->doc);
    free(a->text);
    free(a->closure);
}

/* Reads the artifact; false with why when this host does not read its format,
 * its version is older than the sources' minimum or its closure is not the
 * one its manifest records. */
static bool read_artifact(const compiler_bootstrap *b, int minimum, artifact *a, char *error, size_t size)
{
    char *path = join(b->folder, MANIFEST_FILE), *file = join(b->folder, CLOSURE_FILE);
    size_t length = 0;
    a->text = (char *)read_bytes(path, COMPILER_HOST_MAX_BYTES, &length);
    a->doc = a->text ? yyjson_read(a->text, length, YYJSON_READ_ALLOW_INVALID_UNICODE)
                     : NULL;
    a->root = yyjson_doc_get_root(a->doc);
    a->closure = read_bytes(file, COMPILER_HOST_MAX_ARTIFACT_BYTES, &a->closure_length);
    free(file);
    free(path);
    int64_t format = yyjson_get_sint(yyjson_obj_get(a->root, "format"));
    int64_t version = yyjson_get_sint(yyjson_obj_get(a->root, "version"));
    yyjson_val *closure = yyjson_obj_get(a->root, "closure");
    char digest[65] = "";
    if (a->closure) {
        sha256_hex(a->closure, a->closure_length, digest);
    }
    if (!yyjson_is_obj(a->root)) {
        snprintf(error, size, "%s holds no readable %s", b->folder, MANIFEST_FILE);
        return false;
    }
    if (format != BOOTSTRAP_FORMAT) {
        snprintf(
            error,
            size,
            "%s has format %" PRId64 "; this host reads format %d",
            MANIFEST_FILE,
            format,
            BOOTSTRAP_FORMAT
        );
        return false;
    }
    if (version < minimum) {
        snprintf(
            error,
            size,
            "the artifact has bootstrap version %" PRId64
            ", older than %d, the oldest these compiler "
            "sources support; produce a newer one from stage zero",
            version,
            minimum
        );
        return false;
    }
    if (!a->closure ||
        yyjson_get_uint(yyjson_obj_get(closure, "bytes")) != a->closure_length ||
        !yyjson_equals_str(yyjson_obj_get(closure, "sha256"), digest)) {
        snprintf(
            error,
            size,
            "%s, %zu bytes with SHA-256 %s, is not the closure %s records",
            CLOSURE_FILE,
            a->closure ? a->closure_length : 0,
            digest,
            MANIFEST_FILE
        );
        return false;
    }
    return true;
}

/* Compile's arguments for the sources the folder lists as they are on disk,
 * counting those that differ from what the artifact recorded: new, moved and
 * edited sources and recorded ones the list dropped. NULL with why. */
static char *disk_arguments(
    const compiler_bootstrap *b,
    yyjson_val *root,
    size_t *length,
    int *changed,
    char *error,
    size_t size
)
{
    const char *project = yyjson_get_str(yyjson_obj_get(root, "project"));
    const char *core_path = yyjson_get_str(yyjson_obj_get(root, "core"));
    yyjson_val *recorded_sources = yyjson_obj_get(root, "sources"),
               *target = yyjson_obj_get(root, "target");
    if (!project || !core_path || !yyjson_arr_size(recorded_sources) ||
        yyjson_arr_size(target) != ARGUMENTS - TARGET_FIRST) {
        snprintf(error, size, "%s does not record Compile's arguments", MANIFEST_FILE);
        return NULL;
    }
    source_list sources = { 0 };
    if (!read_sources(b, &sources, error, size)) {
        free_list(&sources);
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *args = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, args);
    yyjson_mut_arr_add_strcpy(doc, args, project);
    yyjson_mut_arr_add_str(doc, yyjson_mut_arr_add_arr(doc, args), "core");
    yyjson_mut_arr_add_strcpy(doc, yyjson_mut_arr_add_arr(doc, args), core_path);
    yyjson_mut_val *paths = yyjson_mut_arr_add_arr(doc, args),
                   *texts = yyjson_mut_arr_add_arr(doc, args);
    bool valid = true;
    size_t before = yyjson_arr_size(recorded_sources);
    *changed = sources.count < before ? (int)(before - sources.count) : 0;
    for (size_t i = 0; valid && i < sources.count; i++) {
        const char *path = sources.paths[i];
        char *file = disk_path(b, core_path, path);
        size_t bytes = 0;
        unsigned char *text = file ? read_bytes(file, COMPILER_HOST_MAX_BYTES, &bytes)
                                   : NULL;
        valid = text != NULL;
        if (valid) {
            char digest[65];
            sha256_hex(text, bytes, digest);
            yyjson_val *before_source = yyjson_arr_get(recorded_sources, i);
            bool same_path = before_source &&
                             yyjson_equals_str(yyjson_obj_get(before_source, "path"), path);
            *changed += !(
                same_path &&
                yyjson_equals_str(yyjson_obj_get(before_source, "sha256"), digest)
            );
            yyjson_mut_arr_add_strcpy(doc, paths, path);
            yyjson_mut_arr_add_strncpy(doc, texts, (const char *)text, bytes);
        } else {
            snprintf(error, size, "cannot read the compiler source %s", path);
        }
        free(text);
        free(file);
    }
    yyjson_mut_arr_add_false(doc, args);
    for (size_t i = 0; i < yyjson_arr_size(target); i++) {
        yyjson_mut_arr_append(args, yyjson_val_mut_copy(doc, yyjson_arr_get(target, i)));
    }
    valid = valid && packages_complete(b, core_path, &sources, error, size);
    char *json = valid ? yyjson_mut_write(doc, YYJSON_WRITE_ALLOW_INVALID_UNICODE, length)
                       : NULL;
    if (valid && !json) {
        snprintf(error, size, "cannot encode Compile's arguments");
    }
    yyjson_mut_doc_free(doc);
    free_list(&sources);
    return json;
}

/* Writes the recovered compiler to output when there is one. */
static bool written(const char *output, const unsigned char *bytes, size_t length, char *report, size_t size)
{
    if (output && !write_path(output, bytes, length)) {
        snprintf(report, size, "%s cannot be written", output);
        return false;
    }
    return true;
}
/* Compiles the sources with the artifact loaded in host, which this frees.
 * When they are the sources the artifact recorded, the result is the
 * artifact's own bytes. Otherwise the artifact only has to compile them, as
 * the seed of a Zig-style bootstrap: it compiles stage one, stage one compiles
 * stage two, and stage two, or stage one when it already is stage two's bytes,
 * must compile the sources to itself, so the artifact's code generation need
 * not match the sources'. Writes that fixed point to `output` when there is
 * one and hands its host to `current`. */
static bool recover(
    compiler_host *host,
    const artifact *a,
    const char *json,
    size_t length,
    int changed,
    const char *output,
    compiler_host **current,
    char *report,
    size_t size
)
{
    size_t first_length = 0, second_length = 0, third_length = 0;
    unsigned char *first = compile(host, json, length, &first_length, report, size);
    compiler_host_free(host);
    if (!first) {
        return false;
    }
    const char *revision = yyjson_get_str(yyjson_obj_get(a->root, "revision"));
    revision = revision && *revision ? revision : "unrecorded";
    char digest[65];
    if (!changed) {
        sha256_hex(first, first_length, digest);
        bool valid = same(first, first_length, a->closure, a->closure_length);
        if (valid) {
            snprintf(
                report,
                size,
                "recovered the compiler of revision %s: %zu bytes, sha256 %s",
                revision,
                first_length,
                digest
            );
        } else {
            snprintf(report, size, "the artifact compiles the sources it records to %zu other bytes", first_length);
        }
        valid = valid && written(output, first, first_length, report, size);
        free(first);
        return valid;
    }
    compiler_host *stage_one = compiler_host_load(first, first_length, behavior_default_backend(), report, size);
    unsigned char *second = stage_one
                              ? compile(stage_one, json, length, &second_length, report, size)
                              : NULL;
    unsigned char *fixed = first, *third = NULL;
    size_t fixed_length = first_length;
    compiler_host *fixed_host = stage_one;
    bool valid = second != NULL;
    if (valid && !same(second, second_length, first, first_length)) {
        /* Stage one generates other code than the artifact did, so stage two
         * is the first compiler built the way the sources build themselves. */
        fixed = second;
        fixed_length = second_length;
        fixed_host = compiler_host_load(second, second_length, behavior_default_backend(), report, size);
        compiler_host_free(stage_one);
        third = fixed_host
                  ? compile(fixed_host, json, length, &third_length, report, size)
                  : NULL;
        valid = third && same(third, third_length, second, second_length);
        if (third && !valid) {
            snprintf(report, size, "the second compiler compiles the sources to %zu other bytes, not to itself", third_length);
        }
    }
    sha256_hex(fixed, fixed_length, digest);
    if (valid) {
        snprintf(
            report,
            size,
            "%d compiler sources changed since revision %s; the artifact "
            "compiled a compiler of %zu bytes%s, sha256 %s, that compiles "
            "them to itself",
            changed,
            revision,
            fixed_length,
            third ? " (after a second compiler)" : "",
            digest
        );
    }
    valid = valid && written(output, fixed, fixed_length, report, size);
    if (valid && current) {
        *current = fixed_host;
        fixed_host = NULL;
    }
    compiler_host_free(fixed_host);
    free(third);
    free(second);
    free(first);
    return valid;
}

static compiler_host *validated(
    const compiler_bootstrap *b,
    artifact *a,
    char **json,
    size_t *length,
    int *changed,
    char *report,
    size_t size
)
{
    contract c;
    bool valid = read_contract(b->compiler, &c, report, size) &&
                 read_artifact(b, c.minimum, a, report, size);
    *json = valid ? disk_arguments(b, a->root, length, changed, report, size) : NULL;
    compiler_host *host = *json ? compiler_host_load(
                                      a->closure,
                                      a->closure_length,
                                      behavior_default_backend(),
                                      report,
                                      size
                                  )
                                : NULL;
    if (host &&
        !target_matches(host, yyjson_obj_get(a->root, "target"), 0, report, size)) {
        compiler_host_free(host);
        host = NULL;
    }
    return host;
}

bool compiler_bootstrap_check(const compiler_bootstrap *b, char *report, size_t size)
{
    artifact a = { 0 };
    char *json = NULL;
    size_t length = 0;
    int changed = 0;
    compiler_host *host = validated(b, &a, &json, &length, &changed, report, size);
    bool valid = host &&
                 recover(host, &a, json, length, changed, b->output, NULL, report, size);
    free(json);
    free_artifact(&a);
    return valid;
}

compiler_host *compiler_bootstrap_load(const compiler_bootstrap *b, char *error, size_t size)
{
    artifact a = { 0 };
    char *json = NULL;
    size_t length = 0;
    int changed = 0;
    compiler_host *host = validated(b, &a, &json, &length, &changed, error, size);
    if (host && changed) {
        compiler_host *current = NULL;
        recover(host, &a, json, length, changed, NULL, &current, error, size);
        host = current;
    }
    free(json);
    free_artifact(&a);
    return host;
}
