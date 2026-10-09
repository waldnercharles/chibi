#include "host/export.h"

#include "host/process.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
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
    while (copied) {
        errno = 0; /* readdir's end and its failure both return NULL */
        struct dirent *entry = readdir(dir);
        if (!entry) {
            copied = errno == 0 || failed(strerror(errno), from);
            break;
        }
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

/* Deletes a folder without following symlinks; false when anything remains. */
static bool remove_tree(const char *path)
{
    struct stat info;
    if (lstat(path, &info) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(info.st_mode)) {
        return unlink(path) == 0;
    }
    DIR *dir = opendir(path);
    bool removed = dir != NULL;
    struct dirent *entry;
    while (dir && (entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
            char *child = join(path, entry->d_name);
            removed = remove_tree(child) && removed;
            free(child);
        }
    }
    if (dir) {
        closedir(dir);
    }
    return rmdir(path) == 0 && removed;
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

/* Where each layout keeps the game's data and the player, inside the output. */
static const struct
{
    const char *data, *executables;
} LAYOUTS[] = {
    [CHIBI_EXPORT_FOLDER] = { "data", NULL },
    [CHIBI_EXPORT_APP] = { "Contents/Resources", "Contents/MacOS" },
};

static bool write_text(const char *path, const char *text)
{
    FILE *file = make_parents(path) ? fopen(path, "wb") : NULL;
    bool written = file && fputs(text, file) >= 0;
    written = file && fclose(file) == 0 && written;
    return written || failed("cannot write", path);
}

static char *read_bytes(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    char *bytes = NULL;
    long length = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (length >= 0 && fseek(file, 0, SEEK_SET) == 0) {
        bytes = xmalloc((size_t)length);
        *size = fread(bytes, 1, (size_t)length, file);
    }
    fclose(file);
    return bytes;
}

/* A reverse-DNS and desktop-entry identifier: lowercase letters, digits and
 * dashes. */
static char *identifier(const char *name)
{
    char *id = xstrdup(name);
    for (char *c = id; *c; c++) {
        if (*c >= 'A' && *c <= 'Z') {
            *c = (char)(*c - 'A' + 'a');
            continue;
        }
        if ((*c < 'a' || *c > 'z') && (*c < '0' || *c > '9')) {
            *c = '-';
        }
    }
    return id;
}

static void put(char **out, const char *text)
{
    for (const char *c = text; *c; c++) {
        apush(*out, *c);
    }
}

/* An XML string: markup characters become entities and control characters,
 * which XML forbids, become spaces. */
static void put_xml(char **out, const char *text)
{
    for (const char *c = text; *c; c++) {
        switch (*c) {
            case '&': put(out, "&amp;"); break;
            case '<': put(out, "&lt;"); break;
            case '>': put(out, "&gt;"); break;
            case '"': put(out, "&quot;"); break;
            default: apush(*out, (unsigned char)*c < ' ' ? ' ' : *c);
        }
    }
}

/* A desktop entry string: one line, with backslashes escaped. */
static void put_line(char **out, const char *text)
{
    for (const char *c = text; *c; c++) {
        if (*c == '\\') {
            put(out, "\\\\");
            continue;
        }
        apush(*out, (unsigned char)*c < ' ' ? ' ' : *c);
    }
}

static bool write_built(const char *path, char **text)
{
    apush(*text, 0);
    bool written = write_text(path, *text);
    afree(*text);
    return written;
}

static uint32_t big_endian(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static void put_big_endian(FILE *file, uint32_t value)
{
    unsigned char bytes[4] = { value >> 24, value >> 16, value >> 8, value };
    fwrite(bytes, 1, 4, file);
}

/* The .icns type that holds a square PNG of this size, or NULL when macOS has
 * no icon of its size. */
static const char *icns_type(const unsigned char *png, size_t size)
{
    static const struct
    {
        uint32_t size;
        const char *type;
    } TYPES[] = { { 16, "icp4" },  { 32, "icp5" },  { 64, "icp6" },
                  { 128, "ic07" }, { 256, "ic08" }, { 512, "ic09" },
                  { 1024, "ic10" } };
    if (size < 24 || memcmp(png, "\x89PNG\r\n\x1a\n", 8) ||
        memcmp(png + 12, "IHDR", 4)) {
        return NULL;
    }
    uint32_t width = big_endian(png + 16);
    for (size_t i = 0; i < sizeof TYPES / sizeof *TYPES; i++) {
        if (width == TYPES[i].size && big_endian(png + 20) == width) {
            return TYPES[i].type;
        }
    }
    return NULL;
}

/* Checks an app's icon before the project compiles. */
static bool app_icon_valid(const chibi_export_spec *spec)
{
    char *path = join(spec->project, spec->icon);
    size_t size = 0;
    unsigned char *png = (unsigned char *)read_bytes(path, &size);
    bool valid = png && icns_type(png, size);
    if (!valid) {
        failed("a macOS app icon must be a square PNG of 16, 32, 64, 128, 256, 512 or 1024 pixels", path);
    }
    free(png);
    free(path);
    return valid;
}

/* An .icns that embeds the PNG as the one icon size it matches. */
static bool write_icns(const char *png, const char *icns)
{
    size_t size = 0;
    unsigned char *bytes = (unsigned char *)read_bytes(png, &size);
    const char *type = bytes ? icns_type(bytes, size) : NULL;
    FILE *file = type && make_parents(icns) ? fopen(icns, "wb") : NULL;
    bool written = file != NULL;
    if (file) {
        fwrite("icns", 1, 4, file);
        put_big_endian(file, (uint32_t)(16 + size));
        fwrite(type, 1, 4, file);
        put_big_endian(file, (uint32_t)(8 + size));
        written = fwrite(bytes, 1, size, file) == size;
        written = fclose(file) == 0 && written;
    }
    free(bytes);
    return written || failed("cannot write", icns);
}

/* The name people see: the title, else the project name, else the
 * executable's. */
static const char *display_name(const chibi_export_spec *spec, const char *name)
{
    if (spec->title) {
        return spec->title;
    }
    return spec->name ? spec->name : name;
}

static bool write_info_plist(const chibi_export_spec *spec, const char *staging, const char *name)
{
    const char *title = display_name(spec, name);
    char *id = identifier(name);
    char *text = NULL;
    put(&text,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n"
        "\t<key>CFBundleDevelopmentRegion</key><string>en</string>\n"
        "\t<key>CFBundleExecutable</key><string>");
    put_xml(&text, name);
    put(&text, "</string>\n\t<key>CFBundleIdentifier</key><string>games.chibi.");
    put(&text, id);
    put(&text,
        "</string>\n"
        "\t<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>\n"
        "\t<key>CFBundleName</key><string>");
    put_xml(&text, title);
    put(&text, "</string>\n\t<key>CFBundleDisplayName</key><string>");
    put_xml(&text, title);
    put(&text,
        "</string>\n"
        "\t<key>CFBundlePackageType</key><string>APPL</string>\n"
        "\t<key>CFBundleShortVersionString</key><string>1.0</string>\n"
        "\t<key>CFBundleVersion</key><string>1</string>\n"
        "\t<key>LSApplicationCategoryType</"
        "key><string>public.app-category.games</string>\n"
        "\t<key>NSHighResolutionCapable</key><true/>\n");
    if (spec->icon) {
        put(&text, "\t<key>CFBundleIconFile</key><string>");
        put_xml(&text, name);
        put(&text, ".icns</string>\n");
    }
    put(&text, "</dict>\n</plist>\n");
    char *path = join(staging, "Contents/Info.plist");
    bool written = write_built(path, &text);
    free(path);
    free(id);
    return written;
}

/* A desktop entry for installing the game: it names the executable on PATH and
 * the icon by its theme name, both the game's identifier. */
static bool write_desktop_entry(const chibi_export_spec *spec, const char *staging, const char *name)
{
    char *id = identifier(name);
    char *text = NULL;
    put(&text, "[Desktop Entry]\nType=Application\nName=");
    put_line(&text, display_name(spec, name));
    put(&text, "\nExec=\"");
    put(&text, name);
    put(&text, "\"\nIcon=");
    put(&text, id);
    put(&text, "\nTerminal=false\nCategories=Game;\n");
    size_t size = strlen(staging) + strlen(id) + sizeof "/.desktop";
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s.desktop", staging, id);
    bool written = write_built(path, &text);
    free(path);
    free(id);
    return written;
}

/* Signs a macOS app ad hoc, so it runs after its contents changed. */
static void sign(const char *staging)
{
#ifdef __APPLE__
    char *argv[] = { "/usr/bin/codesign", "--force", "--sign", "-",
                     (char *)staging,     NULL };
    if (chibi_process_run(argv) != 0) {
        fprintf(stderr, "Export warning: the app could not be signed: %s\n", staging);
    }
#else
    (void)staging;
#endif
}

/* Adds an app's Info.plist and icon, then signs it. */
static bool finish_app(const chibi_export_spec *spec, const char *staging, const char *data, const char *name)
{
    if (!write_info_plist(spec, staging, name)) {
        return false;
    }
    if (spec->icon) {
        char *png = join(data, spec->icon);
        size_t size = strlen(data) + strlen(name) + sizeof "/.icns";
        char *icns = xmalloc(size);
        snprintf(icns, size, "%s/%s.icns", data, name);
        bool written = write_icns(png, icns);
        free(icns);
        free(png);
        if (!written) {
            return false;
        }
    }
    sign(staging);
    return true;
}

static bool assemble(const chibi_export_spec *spec, const char *staging)
{
    char *data = join(staging, LAYOUTS[spec->layout].data);
    char *artifact = join(data, EXPORT_ARTIFACT);
    char *engine_files_dir = join(data, "engine");
    char *compiler = join(spec->bin, "chibi-compile");
    char *player = join(spec->bin, "chibi-player");
    char *engine = join(spec->bin, "../share/chibi");
    char *name = executable_name(spec->name);
    const char *executables = LAYOUTS[spec->layout].executables;
    char *executable_dir = executables ? join(staging, executables) : xstrdup(staging);
    char *executable = join(executable_dir, name);
    char *argv[] = { compiler,   "--project-root", (char *)spec->project,
                     "--output", artifact,         NULL };
    bool assembled = make_parents(artifact) || failed(strerror(errno), data);
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
    if (assembled) {
        assembled = spec->layout == CHIBI_EXPORT_APP
                      ? finish_app(spec, staging, data, name)
                      : write_desktop_entry(spec, staging, name);
    }
    free(executable);
    free(executable_dir);
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
    bool exported = false;
    for (size_t i = 0; S_ISDIR(info.st_mode) && i < sizeof LAYOUTS / sizeof *LAYOUTS;
         i++) {
        char *data = join(output, LAYOUTS[i].data);
        char *marker = join(data, EXPORT_ARTIFACT);
        exported |= access(marker, F_OK) == 0;
        free(marker);
        free(data);
    }
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
    if (published && !remove_tree(old)) {
        fprintf(stderr, "Could not remove the previous export: %s\n", old);
    }
    if (!published) {
        failed(strerror(errno), output);
        rmdir(old); /* only the empty reservation; a stranded export stays */
    }
    free(old);
    return published;
}

/* Writes <output>.tar.gz holding the output folder under its own name. */
/* Writes <output>.tar.gz through a partial file, so a failed tar leaves an
 * earlier archive in place. */
static bool archive(const char *output)
{
    size_t size = strlen(output) + sizeof ".tar.gz.XXXXXX";
    char *path = xmalloc(size), *partial = xmalloc(size);
    snprintf(path, size, "%s.tar.gz", output);
    /* A new file of its own, so concurrent exports and a planted link
     * cannot receive this archive. */
    snprintf(partial, size, "%s.tar.gz.XXXXXX", output);
    int fd = mkstemp(partial);
    if (fd < 0) {
        failed(strerror(errno), partial);
        free(partial);
        free(path);
        return false;
    }
    fchmod(fd, 0644);
    close(fd);
    char *parent = xstrdup(output);
    char *slash = strrchr(parent, '/');
    const char *base = slash ? slash + 1 : output;
    if (slash) {
        *slash = 0;
    }
    /* ./ keeps a name that starts with a dash from reading as an option. */
    char *member = join(".", base);
    char *argv[] = {
        "tar",  "-czf", partial, "-C", slash ? (parent[0] ? parent : "/") : ".",
        member, NULL
    };
    bool archived = chibi_process_run(argv) == 0 && rename(partial, path) == 0;
    if (archived) {
        printf("Archived %s\n", path);
    } else {
        unlink(partial);
        failed("tar could not archive the export", path);
    }
    free(member);
    free(parent);
    free(partial);
    free(path);
    return archived;
}

/* The output without trailing slashes, with .app added to an app's, or NULL
 * when its last component does not name a folder. */
static char *output_path(const chibi_export_spec *spec)
{
    size_t length = strlen(spec->output);
    while (length > 1 && spec->output[length - 1] == '/') {
        length--;
    }
    char *output = xmalloc(length + sizeof ".app");
    memcpy(output, spec->output, length);
    output[length] = 0;
    const char *slash = strrchr(output, '/');
    const char *last = slash ? slash + 1 : output;
    size_t last_length = strlen(last);
    bool app = last_length >= 4 && !strcasecmp(last + last_length - 4, ".app");
    if (!last[0] || !strcmp(last, ".") || !strcmp(last, "..") ||
        (app && last_length == 4)) {
        free(output);
        return NULL;
    }
    if (spec->layout == CHIBI_EXPORT_APP && !app) {
        strcat(output, ".app");
    }
    return output;
}

bool chibi_export(const chibi_export_spec *spec)
{
    assert(spec->layout == CHIBI_EXPORT_FOLDER || spec->layout == CHIBI_EXPORT_APP);
    char *output = output_path(spec);
    if (!output) {
        return failed("the output must name a folder", spec->output);
    }
    if (spec->layout == CHIBI_EXPORT_APP && spec->icon && !app_icon_valid(spec)) {
        free(output);
        return false;
    }
    size_t size = strlen(output) + sizeof ".partial-XXXXXX";
    char *staging = xmalloc(size);
    snprintf(staging, size, "%s.partial-XXXXXX", output);
    bool exported = false;
    if (!make_parents(output) || !mkdtemp(staging)) {
        failed(strerror(errno), output);
    } else {
        exported = chmod(staging, 0755) == 0 && assemble(spec, staging) &&
                   publish(staging, output);
        remove_tree(staging);
    }
    free(staging);
    if (exported) {
        printf("Exported %s\n", output);
    }
    exported = exported && (!spec->archive || archive(output));
    free(output);
    return exported;
}
