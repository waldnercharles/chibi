#include "host/export.h"

#include "host/process.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yyjson.h>

#define EXPORT_ARTIFACT "game.chibip"
#define EXPORT_MANIFEST EXPORT_ARTIFACT ".resources.json"

static char *join(const char *base, const char *name)
{
    size_t size = strlen(base) + strlen(name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", base, name);
    return path;
}

static bool failed(const char *message, const char *path)
{
    fprintf(stderr, "Export failed: %s: %s\n", message, path);
    return false;
}

/* A real directory; a symlinked one is copied as a file and fails, so a link
 * cycle cannot recurse forever. */
static bool is_directory(const char *path)
{
    struct stat info;
    return lstat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

/* Creates every missing directory above `path`, a file to be written. */
static bool make_parents(const char *path)
{
    char *parent = xstrdup(path);
    bool made = true;
    for (char *slash = strchr(parent + 1, '/'); made && slash;
         slash = strchr(slash + 1, '/')) {
        *slash = 0;
        made = mkdir(parent, 0755) == 0 || errno == EEXIST;
        *slash = '/';
    }
    free(parent);
    return made;
}

static bool copy_file(const char *from, const char *to, mode_t mode)
{
    if (!make_parents(to)) {
        return failed("cannot create the folder for", to);
    }
    FILE *in = fopen(from, "rb");
    if (!in) {
        return failed("missing", from);
    }
    FILE *out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return failed("cannot write", to);
    }
    char buffer[65536];
    size_t read;
    bool copied = true;
    while (copied && (read = fread(buffer, 1, sizeof buffer, in))) {
        copied = fwrite(buffer, 1, read, out) == read;
    }
    copied = !ferror(in) && copied;
    fclose(in);
    copied = fclose(out) == 0 && copied && chmod(to, mode) == 0;
    return copied || failed("cannot copy", from);
}

static bool copy_tree(const char *from, const char *to)
{
    DIR *dir = opendir(from);
    if (!dir) {
        return failed("missing", from);
    }
    bool copied = true;
    struct dirent *entry;
    while (copied && (entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        char *source = join(from, entry->d_name);
        char *target = join(to, entry->d_name);
        copied = is_directory(source) ? copy_tree(source, target)
                                      : copy_file(source, target, 0644);
        free(source);
        free(target);
    }
    closedir(dir);
    return copied;
}

/* Deletes a folder this export created, without following symlinks. */
static void remove_tree(const char *path)
{
    struct stat info;
    if (lstat(path, &info) != 0) {
        return;
    }
    if (!S_ISDIR(info.st_mode)) {
        unlink(path);
        return;
    }
    DIR *dir = opendir(path);
    struct dirent *entry;
    while (dir && (entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
            char *child = join(path, entry->d_name);
            remove_tree(child);
            free(child);
        }
    }
    if (dir) {
        closedir(dir);
    }
    rmdir(path);
}

/* Copies each resource the artifact reads from its resource root at run time,
 * then drops the compiler's manifest, which the player does not read. */
static bool copy_external(const char *project, const char *data)
{
    char *manifest = join(data, EXPORT_MANIFEST);
    yyjson_doc *doc = yyjson_read_file(manifest, 0, NULL, NULL);
    yyjson_val *records = yyjson_obj_get(yyjson_doc_get_root(doc), "external");
    bool copied = doc && yyjson_is_arr(records);
    if (!copied) {
        failed("the compiler wrote no resource manifest", manifest);
        yyjson_doc_free(doc);
        free(manifest);
        return false;
    }
    size_t index, count;
    yyjson_val *record;
    yyjson_arr_foreach(records, index, count, record)
    {
        const char *path = yyjson_get_str(yyjson_obj_get(record, "path"));
        if (!path) {
            copied = failed("an external resource has no path", manifest);
            break;
        }
        char *from = join(project, path);
        char *to = join(data, path);
        copied = copy_file(from, to, 0644);
        free(from);
        free(to);
        if (!copied) {
            break;
        }
    }
    yyjson_doc_free(doc);
    unlink(manifest);
    free(manifest);
    return copied;
}

/* The executable's file name: the project name with path and shell
 * metacharacters replaced, and never the data folder's name. */
static char *executable_name(const char *name)
{
    char *file = xstrdup(name && name[0] && strcasecmp(name, "data") ? name : "game");
    for (char *c = file; *c; c++) {
        bool plain = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                     (*c >= '0' && *c <= '9') || *c == ' ' || *c == '-' ||
                   *c == '_' || (*c == '.' && c != file);
        if (!plain) {
            *c = '_';
        }
    }
    return file;
}

static bool assemble(const chibi_export_spec *spec, const char *staging)
{
    char *data = join(staging, "data");
    char *artifact = join(data, EXPORT_ARTIFACT);
    char *engine_files_dir = join(data, "engine");
    char *compiler = join(spec->bin, "chibi-compile");
    char *player = join(spec->bin, "chibi-player");
    char *engine = join(spec->bin, "../share/chibi");
    char *name = executable_name(spec->name);
    char *executable = join(staging, name);
    char *argv[] = { compiler,   "--project-root", (char *)spec->project,
                     "--output", artifact,         NULL };
    bool assembled = mkdir(data, 0755) == 0 || failed(strerror(errno), data);
    if (assembled && chibi_process_run(argv) != 0) {
        assembled = failed("the project did not compile", spec->project);
    }
    assembled = assembled && copy_external(spec->project, data);
    if (assembled && spec->icon) {
        char *from = join(spec->project, spec->icon);
        char *to = join(data, spec->icon);
        assembled = copy_file(from, to, 0644);
        free(from);
        free(to);
    }
    static const char *const engine_files[] = { "shaders", "licenses", "engine.json" };
    for (size_t i = 0; assembled && i < sizeof engine_files / sizeof *engine_files;
         i++) {
        char *from = join(engine, engine_files[i]);
        char *to = join(engine_files_dir, engine_files[i]);
        assembled = is_directory(from) ? copy_tree(from, to)
                                       : copy_file(from, to, 0644);
        free(from);
        free(to);
    }
    assembled = assembled && copy_file(player, executable, 0755);
    free(executable);
    free(name);
    free(engine);
    free(player);
    free(compiler);
    free(engine_files_dir);
    free(artifact);
    free(data);
    return assembled;
}

/* Moves the finished folder into place. An earlier export is set aside first
 * and deleted only once the new one is in place; anything else is refused. */
static bool publish(const char *staging, const char *output)
{
    struct stat info;
    if (lstat(output, &info) != 0) {
        return rename(staging, output) == 0 || failed(strerror(errno), output);
    }
    char *marker = join(output, "data/" EXPORT_ARTIFACT);
    bool exported = S_ISDIR(info.st_mode) && access(marker, F_OK) == 0;
    free(marker);
    if (!exported) {
        return failed("the output exists and is not an exported game", output);
    }
    size_t size = strlen(output) + sizeof ".old-XXXXXX";
    char *old = xmalloc(size);
    snprintf(old, size, "%s.old-XXXXXX", output);
    bool published = mkdtemp(old) && rename(output, old) == 0;
    if (published && rename(staging, output) != 0) {
        published = false;
        rename(old, output);
    }
    if (published) {
        remove_tree(old);
    } else {
        failed(strerror(errno), output);
        rmdir(old); /* only the empty reservation; a stranded export stays */
    }
    free(old);
    return published;
}

/* The output's last path component, which must name a new folder. */
static bool plain_output(const char *output)
{
    const char *slash = strrchr(output, '/');
    const char *last = slash ? slash + 1 : output;
    return strcmp(last, ".") && strcmp(last, "..") && last[0];
}

bool chibi_export(const chibi_export_spec *spec)
{
    size_t length = strlen(spec->output);
    while (length > 1 && spec->output[length - 1] == '/') {
        length--;
    }
    char *output = xmalloc(length + 1);
    memcpy(output, spec->output, length);
    output[length] = 0;
    char *staging = xmalloc(length + sizeof ".partial-XXXXXX");
    snprintf(staging, length + sizeof ".partial-XXXXXX", "%s.partial-XXXXXX", output);
    bool exported = false;
    if (!plain_output(output)) {
        failed("the output must name a folder", output);
    } else if (!make_parents(output) || !mkdtemp(staging)) {
        failed(strerror(errno), output);
    } else {
        exported = chmod(staging, 0755) == 0 && assemble(spec, staging) &&
                   publish(staging, output);
        remove_tree(staging);
    }
    if (exported) {
        printf("Exported %s\n", output);
    }
    free(staging);
    free(output);
    return exported;
}
