#include "reflect_internal.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

int g_failed;
const collect_ctx *g_collection;
static char g_prefix_storage[128];
const char *g_prefix = "REFLECTION";

/* Keep the lock inode stable: other native build directories share it. */
static int lock_manifest(const char *path)
{
    if (!path) {
        return -1;
    }
    char *name = xmalloc(strlen(path) + sizeof ".lock");
    snprintf(name, strlen(path) + sizeof ".lock", "%s.lock", path);
    int fd = open(name, O_CREAT | O_RDWR, 0666);
    free(name);
    if (fd < 0 || flock(fd, LOCK_EX) < 0) {
        perror("reflect: manifest lock");
        exit(1);
    }
    return fd;
}

FILE *begin_generated(const char *path, char **temporary)
{
    *temporary = xmalloc(strlen(path) + sizeof ".XXXXXX");
    snprintf(*temporary, strlen(path) + sizeof ".XXXXXX", "%s.XXXXXX", path);
    int fd = mkstemp(*temporary);
    FILE *file = fd < 0 ? NULL : fdopen(fd, "w+");
    if (!file) {
        perror("reflect: temporary output");
        if (fd >= 0) {
            close(fd);
        }
        unlink(*temporary);
        free(*temporary);
        *temporary = NULL;
        g_failed = 1;
    }
    return file;
}

void publish_generated(FILE *file, char *temporary, const char *path)
{
    int failed = ferror(file) || fflush(file);
    FILE *old = fopen(path, "rb");
    int equal = old != NULL && !failed;
    rewind(file);
    if (old) {
        int a, b;
        do {
            a = fgetc(file);
            b = fgetc(old);
            if (a != b) {
                equal = 0;
            }
        } while (equal && a != EOF);
        if (ferror(old) || ferror(file)) {
            equal = 0;
        }
        fclose(old);
    }
    failed |= fclose(file) != 0;
    if (!failed && !equal && rename(temporary, path)) {
        failed = 1;
    }
    if (failed) {
        fprintf(stderr, "reflect: could not publish %s\n", path);
        g_failed = 1;
    }
    unlink(temporary);
    free(temporary);
}

static void parent_dir(const char *path, char *buf, size_t bufsz)
{
    const char *slash = strrchr(path, '/');
    if (!slash) {
        strcpy(buf, ".");
        return;
    }
    size_t n = (size_t)(slash - path);
    if (n == 0) {
        strcpy(buf, "/");
        return;
    }
    if (n >= bufsz) {
        n = bufsz - 1;
    }
    memcpy(buf, path, n);
    buf[n] = '\0';
}

/* basename of the header that declares cursor c (e.g. "vec3.h"). */
static void set_prefix(const char *source)
{
    const char *base = strrchr(source, '/');
    base = base ? base + 1 : source;
    int length = 0;
    while (base[length] && base[length] != '.' &&
           length < (int)sizeof g_prefix_storage - 1) {
        length++;
    }
    int out = 0;
    for (int i = 0; i < length; i++) {
        char c = base[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            c = '_';
        }
        if (out == 0 && c >= '0' && c <= '9') {
            g_prefix_storage[out++] = '_';
        }
        g_prefix_storage[out++] = c;
    }
    if (out == 0) {
        strcpy(g_prefix_storage, "REFLECTION");
    } else {
        g_prefix_storage[out] = '\0';
    }
    g_prefix = g_prefix_storage;
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: reflect <input.c> --types-output path --runtime-output path [--prefix name] [--manifest path] [--include path ...] [--sysroot path]\n");
        return 2;
    }
    const char *input = argv[1];
    int option = 2;
    const char *prefix = NULL;
    const char *manifest = NULL;
    const char *runtime_output = NULL;
    const char *types_output = NULL;

    char srcdir[1024];
    char rootdir[1024];
    parent_dir(input, srcdir, sizeof srcdir);
    parent_dir(srcdir, rootdir, sizeof rootdir);

    CXIndex idx = clang_createIndex(0, 0);

    const char *args[34] = { "-x", "c", "-fparse-all-comments", "-I", rootdir };
    int nargs = 5;
    while (option < argc) {
        if (option + 1 >= argc || (strcmp(argv[option], "--include") != 0 &&
                                   strcmp(argv[option], "--sysroot") != 0 &&
                                   strcmp(argv[option], "--manifest") != 0 &&
                                   strcmp(argv[option], "--runtime-output") != 0 &&
                                   strcmp(argv[option], "--types-output") != 0 &&
                                   strcmp(argv[option], "--prefix") != 0)) {
            fprintf(stderr, "reflect: invalid output/include option\n");
            clang_disposeIndex(idx);
            return 2;
        }
        if (strcmp(argv[option], "--prefix") == 0) {
            prefix = argv[option + 1];
            option += 2;
            continue;
        }
        if (strcmp(argv[option], "--types-output") == 0) {
            types_output = argv[option + 1];
            option += 2;
            continue;
        }
        if (strcmp(argv[option], "--runtime-output") == 0) {
            runtime_output = argv[option + 1];
            option += 2;
            continue;
        }
        if (strcmp(argv[option], "--manifest") == 0) {
            manifest = argv[option + 1];
            option += 2;
            continue;
        }
        if (nargs + 2 > 32) {
            fprintf(stderr, "reflect: too many include options\n");
            clang_disposeIndex(idx);
            return 2;
        }
        args[nargs++] = strcmp(argv[option], "--sysroot") == 0 ? "-isysroot" : "-I";
        args[nargs++] = argv[option + 1];
        option += 2;
    }

    /* User -I paths take precedence over this installation's builtin headers. */
    if (nargs + 2 > (int)(sizeof args / sizeof *args)) {
        fprintf(stderr, "reflect: too many include options\n");
        clang_disposeIndex(idx);
        return 2;
    }
    args[nargs++] = "-isystem";
    args[nargs++] = CHIBI_REFLECT_RESOURCE_INCLUDE_DIR;

    if (!types_output || !*types_output || !runtime_output || !*runtime_output ||
        !strcmp(types_output, runtime_output) || (prefix && !*prefix)) {
        fprintf(stderr, "reflect: distinct --types-output and --runtime-output paths are required\n");
        clang_disposeIndex(idx);
        return 2;
    }
    set_prefix(prefix ? prefix : types_output);

    CXTranslationUnit tu = clang_parseTranslationUnit(idx, input, args, nargs, NULL, 0, CXTranslationUnit_None);
    if (!tu) {
        fprintf(stderr, "reflect: failed to parse %s\n", input);
        clang_disposeIndex(idx);
        return 1;
    }
    unsigned ndiag = clang_getNumDiagnostics(tu);
    for (unsigned i = 0; i < ndiag; i++) {
        CXDiagnostic d = clang_getDiagnostic(tu, i);
        if (clang_getDiagnosticSeverity(d) >= CXDiagnostic_Error) {
            g_failed = 1;
        }
        CXString s = clang_formatDiagnostic(d, clang_defaultDiagnosticDisplayOptions());
        fprintf(stderr, "reflect: %s\n", clang_getCString(s));
        clang_disposeString(s);
        clang_disposeDiagnostic(d);
    }

    int manifest_lock = lock_manifest(manifest);
    collect_ctx ctx = { NULL, NULL };
    load_component_manifest(manifest);
    reflect_collect(tu, &ctx);
    if (g_failed) {
        goto cleanup;
    }
    g_collection = &ctx;
    for (int i = 0; i < acount(ctx.structs); i++) {
        const struct_info *s = &ctx.structs[i];
        if (s->derived_from && !derived_source(s)) {
            fprintf(stderr, "reflect: %s derives from %s, which is not a component\n", s->name, s->derived_from);
            g_failed = 1;
        }
    }
    record_new_components(&ctx);
    if (g_failed) {
        goto cleanup;
    }

    char *types_temporary = NULL;
    FILE *types = begin_generated(types_output, &types_temporary);
    if (!types) {
        goto cleanup;
    }
    char *runtime_temporary = NULL;
    FILE *runtime = begin_generated(runtime_output, &runtime_temporary);
    if (!runtime) {
        fclose(types);
        unlink(types_temporary);
        free(types_temporary);
        goto cleanup;
    }
    emit_types(types, &ctx);
    emit_runtime(runtime, &ctx);
    publish_generated(types, types_temporary, types_output);
    publish_generated(runtime, runtime_temporary, runtime_output);
    if (g_failed) {
        goto cleanup;
    }
    write_component_manifest();
    if (g_failed) {
        goto cleanup;
    }

    printf(
        "reflect: %d struct(s), %d function(s) -> %s, %s\n",
        acount(ctx.structs),
        acount(ctx.fns),
        types_output,
        runtime_output
    );
    for (int i = 0; i < acount(ctx.structs); i++) {
        printf(
            "reflect:   struct %s (%d fields)\n",
            ctx.structs[i].name,
            acount(ctx.structs[i].fields)
        );
    }
    for (int i = 0; i < acount(ctx.fns); i++) {
        const fn_info *f = &ctx.fns[i];
        printf("reflect:   fn %s(", f->name);
        for (int j = 0; j < acount(f->pname); j++) {
            printf("%s%s %s", j ? ", " : "", f->ptype[j], f->pname[j]);
        }
        printf(") -> %s\n", f->ret);
    }

    reflect_dispose(&ctx);
    dispose_component_manifest();
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(idx);
    if (manifest_lock >= 0) {
        close(manifest_lock);
    }
    return 0;

cleanup:
    if (manifest_lock >= 0) {
        close(manifest_lock);
    }
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(idx);
    reflect_dispose(&ctx);
    dispose_component_manifest();
    return 1;
}
