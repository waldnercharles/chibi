#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "yyjson.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A file's access and modification times; macOS names them apart. */
#ifdef __APPLE__
#define ACCESSED(s) ((s).st_atimespec)
#define MODIFIED(s) ((s).st_mtimespec)
#else
#define ACCESSED(s) ((s).st_atim)
#define MODIFIED(s) ((s).st_mtim)
#endif

static char directory[1024], build[1024], stage[1024], source[1024], log_path[1024];

static void path(char *out, const char *base, const char *name)
{
    int n = snprintf(out, 1024, "%s/%s", base, name);
    assert(n > 0 && n < 1024);
}

static int inspect()
{
    /* Only production packaging functions run in Python. C owns the fixture,
     * mutations and assertions; produced fixture executables never run. */
    const char *script = "import json,sys\nfrom pathlib import Path\n"
                         "sys.path.insert(0,str(Path(sys.argv[1])/'tools'))\n"
                         "import package_sdk as p\n"
                         "p.ROOT=Path(sys.argv[2]);b=Path(sys.argv[3]);s=Path("
                         "sys.argv[4])\n"
                         "c=p.cache_values(b);r=p.build_identity(b,c)\n"
                         "f=p.package_files(s);i=p.installed_archive_owners(r,"
                         "f,'',False)\n"
                         "t=p.installed_private_tools(r,f,s)\n"
                         "print(json.dumps({'compiled':r,'installation':i,'"
                         "files':f},sort_keys=True))\n";
    const char *args[] = { "python3", "-B",  "-c",  script, tool_root(),
                           source,    build, stage, NULL };
    return tool_run(args, directory, log_path);
}

static bool diagnostic(int status, const char *expected)
{
    char *text = tool_read(log_path);
    bool found = text && strstr(text, expected);
    free(text);
    REQUIRE(status == 1);
    REQUIRE(found);
    return true;
}

static bool text_is(yyjson_val *object, const char *name, const char *expected)
{
    const char *actual = yyjson_get_str(yyjson_obj_get(object, name));
    return actual && !strcmp(actual, expected);
}

static bool contains(yyjson_val *array, const char *expected)
{
    size_t i, count;
    yyjson_val *value;
    yyjson_arr_foreach(array, i, count, value)
    {
        const char *text = yyjson_get_str(value);
        if (text && !strcmp(text, expected)) {
            return true;
        }
    }
    return false;
}

static bool schedule_input(const char *filename, const char *output, const char *input, bool validation)
{
    char *text = tool_read(filename);
    REQUIRE(text);
    char prefix[1200], token[1200];
    snprintf(prefix, sizeof prefix, "build %s:", output);
    snprintf(token, sizeof token, " %s", input);
    char *line = strstr(text, prefix);
    REQUIRE(line && (line == text || line[-1] == '\n'));
    char *end = strchr(line, '\n');
    char *at = strstr(line, token);
    REQUIRE(end && at && at < end);
    REQUIRE(at[strlen(token)] == ' ' || at[strlen(token)] == '\n');
    char *order = strstr(line, " || ");
    REQUIRE(validation || (order && order < end));
    memmove(at, at + strlen(token), strlen(at + strlen(token)) + 1);
    end = strchr(line, '\n');
    size_t offset = (size_t)(end - text);
    const char *marker = validation ? " |@ " : " ";
    size_t extra = strlen(marker) + strlen(input);
    char *changed = malloc(strlen(text) + extra + 1);
    REQUIRE(changed);
    memcpy(changed, text, offset);
    memcpy(changed + offset, marker, strlen(marker));
    memcpy(changed + offset + strlen(marker), input, strlen(input));
    strcpy(changed + offset + extra, text + offset);
    tool_write(filename, changed);
    free(changed);
    free(text);
    return true;
}

static bool remove_compile_command(const char *filename, const char *input)
{
    yyjson_doc *doc = yyjson_read_file(filename, 0, NULL, NULL);
    REQUIRE(doc);
    yyjson_val *commands = yyjson_doc_get_root(doc);
    size_t i, count, found = yyjson_arr_size(commands);
    yyjson_val *command;
    yyjson_arr_foreach(commands, i, count, command)
    {
        const char *file = yyjson_get_str(yyjson_obj_get(command, "file"));
        if (file && !strcmp(file, input)) {
            REQUIRE(found == yyjson_arr_size(commands));
            found = i;
        }
    }
    REQUIRE(found < yyjson_arr_size(commands));
    yyjson_mut_doc *changed = yyjson_doc_mut_copy(doc, NULL);
    REQUIRE(changed);
    REQUIRE(yyjson_mut_arr_remove(yyjson_mut_doc_get_root(changed), found));
    REQUIRE(yyjson_mut_write_file(filename, changed, 0, NULL, NULL));
    yyjson_mut_doc_free(changed);
    yyjson_doc_free(doc);
    return true;
}

TEST_CASE(configured_artifacts_preserve_complete_object_ownership)
{
    char root[1200], prefix[1200];
    snprintf(root, sizeof root, "-DCHIBI_ROOT=%s", tool_root());
    snprintf(prefix, sizeof prefix, "-DCMAKE_INSTALL_PREFIX=%s", stage);
    const char *configure[] = {
        "cmake", "-S",   source,  "-B",
        build,   "-G",   "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        root,    prefix, NULL
    };
    REQUIRE(tool_run(configure, directory, log_path) == 0);
    const char *compile[] = { "cmake", "--build", build, "--parallel", "1", NULL };
    REQUIRE(tool_run(compile, directory, log_path) == 0);
    const char *install[] = { "cmake", "--install", build, NULL };
    REQUIRE(tool_run(install, directory, log_path) == 0);
    REQUIRE(inspect() == 0);
    yyjson_doc *doc = yyjson_read_file(log_path, 0, NULL, NULL);
    REQUIRE(doc);
    yyjson_val *result = yyjson_doc_get_root(doc);
    yyjson_val *compiled = yyjson_obj_get(result, "compiled");
    yyjson_val *objects = yyjson_obj_get(compiled, "objects");
    yyjson_val *archives = yyjson_obj_get(compiled, "archives");
#if defined(__linux__) || defined(__APPLE__)
    REQUIRE(yyjson_obj_size(objects) == 10);
#else
    REQUIRE(yyjson_obj_size(objects) == 7);
#endif
    REQUIRE(yyjson_obj_size(archives) == 3);
    REQUIRE(yyjson_obj_get(archives, "relocated/librenamed.a"));
    yyjson_val *object = yyjson_obj_get(objects, "CMakeFiles/folded_object.dir/src/object.c.o");
    REQUIRE(text_is(object, "target", "folded_object"));
    REQUIRE(contains(yyjson_obj_get(object, "artifacts"), "relocated/librenamed.a"));
    object = yyjson_obj_get(objects, "CMakeFiles/folded_static.dir/src/folded.c.o");
    REQUIRE(text_is(object, "target", "folded_static"));
    REQUIRE(yyjson_arr_size(yyjson_obj_get(object, "artifacts")) == 2);
    REQUIRE(contains(yyjson_obj_get(object, "artifacts"), "relocated/librenamed.a"));
    REQUIRE(contains(yyjson_obj_get(object, "artifacts"), "libfolded_static.a"));
    const char *pch = "CMakeFiles/owner.dir/cmake_pch.h.pch";
    object = yyjson_obj_get(objects, pch);
    if (!object) {
        pch = "CMakeFiles/owner.dir/cmake_pch.h.gch";
        object = yyjson_obj_get(objects, pch);
    }
    REQUIRE(contains(yyjson_obj_get(object, "consumers"), "CMakeFiles/owner.dir/src/owned.c.o"));
    yyjson_val *products = yyjson_obj_get(compiled, "executables");
    yyjson_val *auxiliary = yyjson_obj_get(compiled, "auxiliary_executables");
    REQUIRE(yyjson_obj_size(products) == 1);
    REQUIRE(text_is(yyjson_obj_get(products, "chibi-compile"), "path", "chibi-compile"));
    const char *resource = "CMakeFiles/chibi-compile.dir/src/main.res";
    object = yyjson_obj_get(objects, resource);
    REQUIRE(text_is(object, "target", "chibi-compile"));
    REQUIRE(yyjson_arr_size(yyjson_obj_get(object, "artifacts")) == 1);
    REQUIRE(contains(yyjson_obj_get(object, "artifacts"), "chibi-compile"));
    const char *resource_hash = yyjson_get_str(yyjson_obj_get(object, "sha256"));
    REQUIRE(
        resource_hash &&
        text_is(yyjson_obj_get(yyjson_obj_get(products, "chibi-compile"), "link_inputs"), resource, resource_hash)
    );
    REQUIRE(
        text_is(yyjson_obj_get(compiled, "inputs"), "generated/resource.manifest", "e0fb600d5b2b5562d501eaebc89a4d24d5b079bc94b88fa0321e5a822e531f67")
    );
    const char *product_hash = yyjson_get_str(
        yyjson_obj_get(yyjson_obj_get(products, "chibi-compile"), "sha256")
    );
    yyjson_val *installed_product = yyjson_obj_get(yyjson_obj_get(result, "files"), "bin/chibi-compile");
    REQUIRE(product_hash && text_is(installed_product, "sha256", product_hash));
    REQUIRE(yyjson_obj_size(auxiliary) == 1);
    REQUIRE(text_is(yyjson_obj_get(auxiliary, "auxiliary"), "path", "auxiliary"));
    yyjson_val *private_tools = yyjson_obj_get(compiled, "private_executables");
#if defined(__linux__) || defined(__APPLE__)
    REQUIRE(yyjson_obj_size(private_tools) == 2);
    REQUIRE(
        text_is(yyjson_obj_get(private_tools, "chibi-build-host"), "install_path", "libexec/chibi/chibi-build-host")
    );
    REQUIRE(
        text_is(yyjson_obj_get(private_tools, "chibi-compiler-host"), "install_path", "libexec/chibi/chibi-compiler-host")
    );
    object = yyjson_obj_get(objects, "CMakeFiles/chibi_compiler_host.dir/src/debug.c.o");
    REQUIRE(object);
    REQUIRE(contains(yyjson_obj_get(object, "artifacts"), "chibi-compiler-host"));
    REQUIRE(yyjson_obj_get(archives, "libchibi_compiler_host.a") == NULL);
#else
    REQUIRE(yyjson_obj_size(private_tools) == 0);
#endif
    yyjson_val *installation = yyjson_obj_get(result, "installation");
    REQUIRE(
        text_is(yyjson_obj_get(installation, "relocated/librenamed.a"), "path", "lib/librenamed.a")
    );
    REQUIRE(
        text_is(yyjson_obj_get(installation, "libdebug_archive.a"), "path", "lib/libdebug_archive.a")
    );
    REQUIRE(
        contains(yyjson_obj_get(yyjson_obj_get(installation, "libfolded_static.a"), "folded_into"), "relocated/librenamed.a")
    );
    yyjson_doc_free(doc);

    /* Fixed compiled-evidence rejects, restoring each fixture mutation. */
    char filename[1024], saved[1024];
    path(filename, build, "CMakeFiles/folded_object.dir/src/object.c.o");
    path(saved, directory, "saved-object");
    REQUIRE(rename(filename, saved) == 0);
    int status = inspect();
    REQUIRE(rename(saved, filename) == 0);
    REQUIRE(diagnostic(status, "Build is incomplete:"));
    struct stat info;
    REQUIRE(stat(filename, &info) == 0);
    struct timespec times[] = { ACCESSED(info), MODIFIED(info) };
    struct timespec newer[] = { ACCESSED(info), MODIFIED(info) };
    newer[1].tv_sec += 60;
    REQUIRE(utimensat(AT_FDCWD, filename, newer, 0) == 0);
    status = inspect();
    REQUIRE(utimensat(AT_FDCWD, filename, times, 0) == 0);
    REQUIRE(diagnostic(status, "predates a link dependency"));

    char graph_path[1024];
    path(graph_path, build, "build.ninja");
    char *graph = tool_read(graph_path);
    REQUIRE(graph);
#if defined(__linux__) || defined(__APPLE__)
    /* Keeping this object in auxiliary must not substitute for its installed
     * compiler-host owner. The ordinary artifact membership stays complete. */
    REQUIRE(
        schedule_input(graph_path, "chibi-compiler-host", "CMakeFiles/chibi_compiler_host.dir/src/debug.c.o", true)
    );
    status = inspect();
    tool_write(graph_path, graph);
    REQUIRE(diagnostic(status, "Private host object has no installed executable owner:"));
#endif
    REQUIRE(
        schedule_input(graph_path, "relocated/librenamed.a", "CMakeFiles/folded_object.dir/src/object.c.o", true)
    );
    status = inspect();
    tool_write(graph_path, graph);
    REQUIRE(diagnostic(status, "Compiled output has no linked artifact:"));

    /* Resource outputs require a real link edge, just like ordinary objects. */
    REQUIRE(schedule_input(graph_path, "chibi-compile", resource, true));
    status = inspect();
    tool_write(graph_path, graph);
    REQUIRE(diagnostic(status, "Artifact has no compiled object input:"));
    REQUIRE(schedule_input(graph_path, "chibi-compile", resource, false));
    status = inspect();
    tool_write(graph_path, graph);
    REQUIRE(diagnostic(status, "Artifact has no compiled object input:"));

    /* The resource payload is a graph dependency, not a preprocessor include. */
    path(filename, build, "resource.manifest");
    REQUIRE(stat(filename, &info) == 0);
    times[0] = newer[0] = ACCESSED(info);
    times[1] = MODIFIED(info);
    struct stat resource_info;
    path(saved, build, resource);
    REQUIRE(stat(saved, &resource_info) == 0);
    newer[1] = MODIFIED(resource_info);
    newer[1].tv_sec += 60;
    REQUIRE(utimensat(AT_FDCWD, filename, newer, 0) == 0);
    status = inspect();
    REQUIRE(utimensat(AT_FDCWD, filename, times, 0) == 0);
    REQUIRE(
        diagnostic(status, "Compiled dependency changed after its object was built: resource.manifest")
    );

    path(filename, build, "compile_commands.json");
    char *commands = tool_read(filename);
    REQUIRE(commands);
    path(saved, source, "src/main.res");
    REQUIRE(remove_compile_command(filename, saved));
    status = inspect();
    tool_write(filename, commands);
    free(commands);
    REQUIRE(diagnostic(status, "Artifact has an unaccounted object input:"));

    REQUIRE(schedule_input(graph_path, "CMakeFiles/owner.dir/src/owned.c.o", pch, false));
    status = inspect();
    tool_write(graph_path, graph);
    free(graph);
    REQUIRE(diagnostic(status, "Precompiled header has no owned consumer:"));

    path(filename, build, "chibi_package_targets-Release.txt");
    char *manifest = tool_read(filename);
    REQUIRE(manifest);
    char record[1200];
    snprintf(record, sizeof record, "EXECUTABLE\tauxiliary\t%s/auxiliary\n", build);
    FILE *append = fopen(filename, "a");
    REQUIRE(append);
    REQUIRE(fputs(record, append) >= 0);
    REQUIRE(fclose(append) == 0);
    status = inspect();
    tool_write(filename, manifest);
    free(manifest);
    REQUIRE(diagnostic(status, "Duplicate package target or output:"));

    /* Both installed-owner negatives preserve all compiled evidence. */
    path(filename, stage, "lib/librenamed.a");
    path(saved, directory, "saved-archive");
    REQUIRE(rename(filename, saved) == 0);
    status = inspect();
    REQUIRE(rename(saved, filename) == 0);
    REQUIRE(diagnostic(status, "Built archive has no installed owner:"));
    REQUIRE(rename(filename, saved) == 0);
    tool_write(filename, "corrupt archive\n");
    status = inspect();
    REQUIRE(unlink(filename) == 0);
    REQUIRE(rename(saved, filename) == 0);
    REQUIRE(diagnostic(status, "Installed archive differs from compiled evidence:"));

#if defined(__linux__) || defined(__APPLE__)
    /* Private installed tools retain their own compiled identity. */
    path(filename, stage, "libexec/chibi/chibi-compiler-host");
    path(saved, directory, "saved-private-tool");
    REQUIRE(rename(filename, saved) == 0);
    status = inspect();
    REQUIRE(rename(saved, filename) == 0);
    REQUIRE(diagnostic(status, "Installed private executable differs from compiled evidence:"));
    REQUIRE(rename(filename, saved) == 0);
    tool_write(filename, "corrupt private tool\n");
    REQUIRE(chmod(filename, 0700) == 0);
    status = inspect();
    REQUIRE(unlink(filename) == 0);
    REQUIRE(rename(saved, filename) == 0);
    REQUIRE(diagnostic(status, "Installed private executable differs from compiled evidence:"));
#endif
    return true;
}

#if defined(__linux__) || defined(__APPLE__)
TEST_CASE(makefiles_private_tool_owns_direct_host_objects)
{
    path(source, directory, "make-source");
    path(build, directory, "make-build");
    path(stage, directory, "make-installed");
    REQUIRE(mkdir(source, 0700) == 0);
    char filename[1024];
    path(filename, source, "src");
    REQUIRE(mkdir(filename, 0700) == 0);
    path(filename, source, "src/host.h");
    tool_write(filename, "int host_value(void);\n");
    path(filename, source, "src/host.c");
    tool_write(filename, "#include \"host.h\"\nint host_value(void) { return 0; }\n");
    path(filename, source, "src/main.c");
    tool_write(filename, "#include \"host.h\"\nint main(void) { return host_value(); }\n");
    path(filename, source, "CMakeLists.txt");
    tool_write(
        filename,
        "cmake_minimum_required(VERSION 3.25)\nproject(private_tool C)\n"
        "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        "set(CHIBI_SDK_BUILD_COMPILER ON CACHE BOOL \"\")\n"
        "set(CHIBI_SDK_BUILD_NATIVE OFF CACHE BOOL \"\")\n"
        "add_library(chibi_build_host OBJECT src/host.c)\n"
        "add_executable(chibi-build-host src/main.c)\n"
        "target_link_libraries(chibi-build-host PRIVATE chibi_build_host)\n"
        "add_library(chibi_compiler_host OBJECT src/host.c)\n"
        "add_executable(chibi-compiler-host src/main.c)\n"
        "target_link_libraries(chibi-compiler-host PRIVATE "
        "chibi_compiler_host)\n"
        "install(TARGETS chibi-build-host chibi-compiler-host RUNTIME "
        "DESTINATION libexec/chibi)\n"
        "file(SHA256 \"${CMAKE_CURRENT_SOURCE_DIR}/src/host.c\" source_hash)\n"
        "file(SHA256 \"${CMAKE_CURRENT_SOURCE_DIR}/src/host.h\" header_hash)\n"
        "file(WRITE \"${CMAKE_BINARY_DIR}/chibi_engine_metadata.json\" "
        "\"{\\\"project_version\\\":1,\\\"artifact_formats\\\":{"
        "\\\"execution\\\":{"
        "\\\"version\\\":1,\\\"source\\\":\\\"src/host.c\\\","
        "\\\"header\\\":\\\"src/"
        "host.h\\\",\\\"sha256\\\":\\\"${source_hash}\\\","
        "\\\"header_sha256\\\":\\\"${header_hash}\\\"}}}\\n\")\n"
    );
    char prefix[1200];
    snprintf(prefix, sizeof prefix, "-DCMAKE_INSTALL_PREFIX=%s", stage);
    const char *configure[] = {
        "cmake", "-S", source,           "-B",
        build,   "-G", "Unix Makefiles", "-DCMAKE_BUILD_TYPE=Release",
        prefix,  NULL
    };
    REQUIRE(tool_run(configure, directory, log_path) == 0);
    const char *compile[] = { "cmake", "--build", build, "--parallel", "1", NULL };
    REQUIRE(tool_run(compile, directory, log_path) == 0);
    const char *install[] = { "cmake", "--install", build, NULL };
    REQUIRE(tool_run(install, directory, log_path) == 0);
    REQUIRE(inspect() == 0);
    yyjson_doc *doc = yyjson_read_file(log_path, 0, NULL, NULL);
    REQUIRE(doc);
    yyjson_val *compiled = yyjson_obj_get(yyjson_doc_get_root(doc), "compiled");
    REQUIRE(yyjson_obj_size(yyjson_obj_get(compiled, "objects")) == 4);
    REQUIRE(yyjson_obj_size(yyjson_obj_get(compiled, "archives")) == 0);
    REQUIRE(yyjson_obj_size(yyjson_obj_get(compiled, "executables")) == 0);
    REQUIRE(text_is(
        yyjson_obj_get(yyjson_obj_get(compiled, "private_executables"), "chibi-build-host"),
        "install_path",
        "libexec/chibi/chibi-build-host"
    ));
    REQUIRE(yyjson_obj_size(yyjson_obj_get(compiled, "private_executables")) == 2);
    yyjson_val *compiler = yyjson_obj_get(yyjson_obj_get(compiled, "private_executables"), "chibi-compiler-host");
    REQUIRE(text_is(compiler, "install_path", "libexec/chibi/chibi-compiler-host"));
    yyjson_val *objects = yyjson_obj_get(compiled, "objects");
    const char *owned = "CMakeFiles/chibi_compiler_host.dir/src/host.c.o";
    const char *checksum = yyjson_get_str(
        yyjson_obj_get(yyjson_obj_get(objects, owned), "sha256")
    );
    REQUIRE(checksum && text_is(yyjson_obj_get(compiler, "link_inputs"), owned, checksum));
    yyjson_doc_free(doc);
    return true;
}
#endif

void test_tool_package_ownership()
{
    /* Apple install-time ranlib refreshes archive member dates unless build
     * and install use the same deterministic archive environment. */
    const char *archive_date = getenv("ZERO_AR_DATE");
    char *saved_archive_date = archive_date ? strdup(archive_date) : NULL;
    assert(!setenv("ZERO_AR_DATE", "1", 1));
    strcpy(directory, "/tmp/chibi-package-ownership-XXXXXX");
    assert(mkdtemp(directory));
    char *resolved = realpath(directory, NULL);
    assert(resolved && strlen(resolved) < sizeof directory);
    strcpy(directory, resolved);
    free(resolved);
    path(build, directory, "build");
    path(stage, directory, "installed");
    path(source, tool_root(), "cmake/tests/package_ownership");
    path(log_path, directory, "child.log");
    RUN_TEST_CASE(configured_artifacts_preserve_complete_object_ownership);
#if defined(__linux__) || defined(__APPLE__)
    RUN_TEST_CASE(makefiles_private_tool_owns_direct_host_objects);
#endif
    tool_remove_tree(directory);
    if (saved_archive_date) {
        assert(!setenv("ZERO_AR_DATE", saved_archive_date, 1));
    } else {
        assert(!unsetenv("ZERO_AR_DATE"));
    }
    free(saved_archive_date);
}
