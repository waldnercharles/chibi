#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "yyjson.h"

#include <assert.h>
#include <dirent.h>
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
    const char
        *script = "import json,sys\nfrom pathlib import Path\n"
                  "sys.path.insert(0,str(Path(sys.argv[1])/'tools'))\n"
                  "import package_sdk as p\n"
                  "p.ROOT=Path(sys.argv[2]);b=Path(sys.argv[3]);s=Path("
                  "sys.argv[4])\n"
                  "c=p.cache_values(b);r=p.build_identity(b,c)\n"
                  "f=p.package_files(s);i=p.installed_archive_owners(r,"
                  "f)\n"
                  "t=p.installed_private_tools(r,f,s)\n"
                  "e=p.engine_manifest(b/'engine-stage',r)\n"
                  "print(json.dumps({'compiled':r,'installation':i,'"
                  "files':f,'engine':e,'private_tools':t},sort_keys=True))\n";
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

#if defined(__linux__) || defined(__APPLE__)
static bool compiler_archive(yyjson_val *result)
{
    yyjson_val *compiled = yyjson_obj_get(result, "compiled");
    const char *name = "libchibi_compiler_host.a";
    const char *checksum = yyjson_get_str(
        yyjson_obj_get(yyjson_obj_get(compiled, "archives"), name)
    );
    REQUIRE(checksum);
    yyjson_val *compiler = yyjson_obj_get(yyjson_obj_get(compiled, "private_executables"), "chibi-compiler-host");
    REQUIRE(text_is(yyjson_obj_get(compiler, "link_inputs"), name, checksum));
    REQUIRE(
        text_is(yyjson_obj_get(yyjson_obj_get(result, "installation"), name), "path", "lib/libchibi_compiler_host.a")
    );
    REQUIRE(
        text_is(yyjson_obj_get(yyjson_obj_get(result, "files"), "lib/libchibi_compiler_host.a"), "sha256", checksum)
    );
    return true;
}

static bool missing_link_rejected(const char *relative, const char *input, const char *expected)
{
    char filename[1024];
    path(filename, build, relative);
    char *link = tool_read(filename);
    REQUIRE(link);
    char *object = strstr(link, input);
    REQUIRE(object);
    char *changed = strdup(link);
    REQUIRE(changed);
    size_t offset = (size_t)(object - link);
    memmove(
        changed + offset,
        changed + offset + strlen(input),
        strlen(changed + offset + strlen(input)) + 1
    );
    tool_write(filename, changed);
    int status = inspect();
    tool_write(filename, link);
    free(changed);
    free(link);
    return diagnostic(status, expected);
}
#endif

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

TEST_CASE(host_components_match_the_configured_platform)
{
    const char *script =
        "import json,sys\nfrom pathlib import Path\n"
        "sys.path.insert(0,str(Path(sys.argv[1])/'tools'))\n"
        "import package_sdk as p\n"
        "cases=[('sdk','ON','OFF','Linux'),('sdk','ON','ON','Darwin'),"
        "('sdk','ON','OFF','Windows'),('sdk','ON','OFF','Emscripten'),"
        "('runtime','ON','ON','Linux'),('sdk','OFF','OFF','Linux')]\n"
        "result=[sorted(p.host_compiler_components(k,{'CHIBI_SDK_BUILD_"
        "COMPILER':c,"
        "'CHIBI_SDK_HAS_ASSET_COMPILER_HOST':a},s)) for k,c,a,s in cases]\n"
        "try:\n "
        "p.host_compiler_components('sdk',{'CHIBI_SDK_BUILD_COMPILER':'ON',"
        "'CHIBI_SDK_HAS_ASSET_COMPILER_HOST':'ON'},'Windows')\n"
        "except ValueError as error:\n result.append(str(error))\n"
        "assert not ({'compiler_syntax','compiler_json'} & "
        "p.COMPILER_TARGETS)\n"
        "assert {'compiler_syntax','compiler_json'} <= "
        "p.NATIVE_AUTHORING_TARGETS\n"
        "print(json.dumps(result))\n";
    const char *args[] = { "python3", "-B", "-c", script, tool_root(), NULL };
    REQUIRE(tool_run(args, directory, log_path) == 0);
    yyjson_doc *doc = yyjson_read_file(log_path, 0, NULL, NULL);
    REQUIRE(doc);
    yyjson_val *cases = yyjson_doc_get_root(doc);
    REQUIRE(yyjson_arr_size(cases) == 7);
    REQUIRE(yyjson_arr_size(yyjson_arr_get(cases, 0)) == 1);
    REQUIRE(contains(yyjson_arr_get(cases, 0), "compiler_host"));
    REQUIRE(yyjson_arr_size(yyjson_arr_get(cases, 1)) == 2);
    REQUIRE(contains(yyjson_arr_get(cases, 1), "compiler_host"));
    REQUIRE(contains(yyjson_arr_get(cases, 1), "asset_compiler_host"));
    REQUIRE(yyjson_arr_size(yyjson_arr_get(cases, 2)) == 1);
    REQUIRE(contains(yyjson_arr_get(cases, 2), "compiler_host"));
    for (size_t i = 3; i < 6; i++) {
        REQUIRE(yyjson_arr_size(yyjson_arr_get(cases, i)) == 0);
    }
    const char *error = yyjson_get_str(yyjson_arr_get(cases, 6));
    REQUIRE(error && !strcmp(error, "Asset compiler host is unavailable on Windows"));
    yyjson_doc_free(doc);
    return true;
}

TEST_CASE(required_unavailable_host_component_names_the_component)
{
    char config[1024], filename[1024], consumer[1024], producer[1024];
    path(producer, directory, "component-config-source");
    path(config, directory, "component-config");
    path(consumer, directory, "component-consumer");
    REQUIRE(!mkdir(producer, 0700) && !mkdir(config, 0700) && !mkdir(consumer, 0700));
    path(filename, producer, "CMakeLists.txt");
    tool_write(
        filename,
        "cmake_minimum_required(VERSION 3.25)\nproject(component_config NONE)\n"
        "include(CMakePackageConfigHelpers)\n"
        "configure_package_config_file(\"${CHIBI_ROOT}/sdk/"
        "ChibiConfig.cmake.in\" "
        "\"${OUTPUT}/ChibiConfig.cmake\" INSTALL_DESTINATION .)\n"
    );
    char root[1200], output[1200], generated[1024];
    snprintf(root, sizeof root, "-DCHIBI_ROOT=%s", tool_root());
    snprintf(output, sizeof output, "-DOUTPUT=%s", config);
    path(generated, directory, "component-config-build");
    const char *configure[] = { "cmake",   "-S", producer, "-B",
                                generated, root, output,   NULL };
    REQUIRE(tool_run(configure, directory, log_path) == 0);
    path(filename, config, "ChibiTargets.cmake");
    tool_write(filename, "# Deliberately no host components in this SDK fixture.\n");
    path(filename, config, "ChibiVendorDependencies.cmake");
    tool_write(filename, "# No external dependencies in this config-only fixture.\n");
    path(filename, consumer, "CMakeLists.txt");
    tool_write(
        filename,
        "cmake_minimum_required(VERSION 3.25)\nproject(component_consumer "
        "NONE)\n"
        "find_package(Chibi REQUIRED CONFIG COMPONENTS ${COMPONENT})\n"
    );
    const char *names[] = { "compiler_host", "asset_compiler_host" };
    char prefix[1200];
    snprintf(prefix, sizeof prefix, "-DChibi_DIR=%s", config);
    for (int i = 0; i < 2; i++) {
        char component[128], build_name[128], build_dir[1024], expected[128];
        snprintf(component, sizeof component, "-DCOMPONENT=%s", names[i]);
        snprintf(build_name, sizeof build_name, "missing-%s", names[i]);
        path(build_dir, directory, build_name);
        const char *args[] = { "cmake",   "-S",   consumer,  "-B",
                               build_dir, prefix, component, NULL };
        int status = tool_run(args, directory, log_path);
        snprintf(expected, sizeof expected, "Required Chibi component '%s' is unavailable", names[i]);
        REQUIRE(diagnostic(status, expected));
    }
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
    /* The installed build identity is the configured document with a build ID. */
    yyjson_val *engine = yyjson_obj_get(result, "engine");
    REQUIRE(text_is(engine, "sdk_version", "0.1.0"));
    REQUIRE(yyjson_get_int(yyjson_obj_get(engine, "project_version")) == 1);
    REQUIRE(yyjson_get_str(yyjson_obj_get(engine, "build_id")));
    yyjson_val *execution = yyjson_obj_get(yyjson_obj_get(engine, "artifact_formats"), "execution");
    REQUIRE(yyjson_get_int(yyjson_obj_get(execution, "version")) == 1);
    REQUIRE(text_is(execution, "source", "src/owned.c"));
#if defined(__linux__) || defined(__APPLE__)
    REQUIRE(yyjson_obj_size(objects) == 11);
    REQUIRE(yyjson_obj_size(archives) == 4);
#else
    REQUIRE(yyjson_obj_size(objects) == 7);
    REQUIRE(yyjson_obj_size(archives) == 3);
#endif
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
    REQUIRE(contains(yyjson_obj_get(object, "artifacts"), "libchibi_compiler_host.a"));
    REQUIRE(!contains(yyjson_obj_get(object, "artifacts"), "chibi-compiler-host"));
    REQUIRE(compiler_archive(result));
    object = yyjson_obj_get(objects, "CMakeFiles/chibi_build_host.dir/src/debug.c.o");
    REQUIRE(object);
    REQUIRE(contains(yyjson_obj_get(object, "artifacts"), "chibi-build-host"));
    REQUIRE(contains(yyjson_obj_get(object, "artifacts"), "auxiliary"));
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
    char metadata[1024];
    path(metadata, build, "chibi_engine_metadata.json");
    char *configured = tool_read(metadata);
    REQUIRE(configured);
    tool_write(metadata, "{\"project_version\":1,\"artifact_formats\":{\"execution\":{\"version\":1}}}\n");
    status = inspect();
    tool_write(metadata, configured);
    free(configured);
    REQUIRE(diagnostic(status, "Configured SDK version is missing"));

    char graph_path[1024];
    path(graph_path, build, "build.ninja");
    char *graph = tool_read(graph_path);
    REQUIRE(graph);
#if defined(__linux__) || defined(__APPLE__)
    /* Keeping this object in auxiliary must not substitute for its installed
     * build-host owner. The ordinary artifact membership stays complete. */
    REQUIRE(
        schedule_input(graph_path, "chibi-build-host", "CMakeFiles/chibi_build_host.dir/src/debug.c.o", true)
    );
    status = inspect();
    tool_write(graph_path, graph);
    REQUIRE(diagnostic(status, "Private host object has no installed executable owner:"));
    REQUIRE(schedule_input(graph_path, "chibi-compiler-host", "libchibi_compiler_host.a", true));
    status = inspect();
    tool_write(graph_path, graph);
    REQUIRE(diagnostic(status, "Compiler runtime archive has no installed executable consumer:"));
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
static bool private_tool_fixture(bool windows)
{
    path(source, directory, windows ? "windows-source" : "make-source");
    path(build, directory, windows ? "windows-build" : "make-build");
    path(stage, directory, windows ? "windows-installed" : "make-installed");
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
    char cmake[4096];
    int length = snprintf(
        cmake,
        sizeof cmake,
        "cmake_minimum_required(VERSION 3.25)\nproject(private_tool C)\n"
        "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        "set(CHIBI_SDK_BUILD_COMPILER ON CACHE BOOL \"\")\n"
        "set(CHIBI_SDK_BUILD_NATIVE OFF CACHE BOOL \"\")\n"
        "add_library(chibi_compiler_host STATIC src/host.c)\n"
        "add_executable(chibi-compiler-host src/main.c)\n"
        "target_link_libraries(chibi-compiler-host PRIVATE "
        "chibi_compiler_host)\n"
        "%s"
        "install(TARGETS chibi-compiler-host RUNTIME DESTINATION "
        "libexec/chibi)\n"
        "install(TARGETS chibi_compiler_host ARCHIVE DESTINATION lib)\n"
        "file(SHA256 \"${CMAKE_CURRENT_SOURCE_DIR}/src/host.c\" source_hash)\n"
        "file(SHA256 \"${CMAKE_CURRENT_SOURCE_DIR}/src/host.h\" header_hash)\n"
        "file(WRITE \"${CMAKE_BINARY_DIR}/chibi_engine_metadata.json\" "
        "\"{\\\"sdk_version\\\":\\\"0.1.0\\\",\\\"project_version\\\":1,"
        "\\\"artifact_formats\\\":{"
        "\\\"execution\\\":{"
        "\\\"version\\\":1,\\\"source\\\":\\\"src/host.c\\\","
        "\\\"header\\\":\\\"src/"
        "host.h\\\",\\\"sha256\\\":\\\"${source_hash}\\\","
        "\\\"header_sha256\\\":\\\"${header_hash}\\\"}}}\\n\")\n",
        windows ? "set_target_properties(chibi-compiler-host PROPERTIES SUFFIX "
                  "\".exe\")\n"
                : "add_library(chibi_build_host OBJECT src/host.c)\n"
                  "add_executable(chibi-build-host src/main.c)\n"
                  "target_link_libraries(chibi-build-host PRIVATE "
                  "chibi_build_host)\n"
                  "install(TARGETS chibi-build-host RUNTIME DESTINATION "
                  "libexec/chibi)\n"
    );
    REQUIRE(length > 0 && (size_t)length < sizeof cmake);
    tool_write(filename, cmake);
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
    return true;
}

TEST_CASE(makefiles_tools_own_private_objects_and_installed_runtime)
{
    REQUIRE(private_tool_fixture(false));
    REQUIRE(inspect() == 0);
    yyjson_doc *doc = yyjson_read_file(log_path, 0, NULL, NULL);
    REQUIRE(doc);
    yyjson_val *result = yyjson_doc_get_root(doc);
    yyjson_val *compiled = yyjson_obj_get(result, "compiled");
    REQUIRE(yyjson_obj_size(yyjson_obj_get(compiled, "objects")) == 4);
    REQUIRE(yyjson_obj_size(yyjson_obj_get(compiled, "archives")) == 1);
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
    const char *owned = "CMakeFiles/chibi_build_host.dir/src/host.c.o";
    const char *checksum = yyjson_get_str(
        yyjson_obj_get(yyjson_obj_get(objects, owned), "sha256")
    );
    yyjson_val *builder = yyjson_obj_get(yyjson_obj_get(compiled, "private_executables"), "chibi-build-host");
    REQUIRE(checksum && text_is(yyjson_obj_get(builder, "link_inputs"), owned, checksum));
    REQUIRE(compiler_archive(result));
    yyjson_doc_free(doc);
    REQUIRE(
        missing_link_rejected("CMakeFiles/chibi-build-host.dir/link.txt", owned, "Private host object has no installed executable owner:")
    );
    REQUIRE(missing_link_rejected(
        "CMakeFiles/chibi-compiler-host.dir/link.txt",
        "libchibi_compiler_host.a",
        "Compiler runtime archive has no installed executable consumer:"
    ));
    return true;
}

TEST_CASE(windows_private_compiler_owns_exe_without_a_posix_build_host)
{
    REQUIRE(private_tool_fixture(true));
    /* Compile tiny native files with the Windows output name, then select the
     * Windows packaging contract. No fixture executable runs, and this is not
     * a Windows compiler or PE-format validation. */
    char metadata[1024], filename[1024], saved[1024];
    path(metadata, build, "CMakeFiles");
    DIR *files = opendir(metadata);
    REQUIRE(files);
    struct dirent *entry;
    int systems = 0;
    while ((entry = readdir(files))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        char folder[1024];
        path(folder, metadata, entry->d_name);
        path(filename, folder, "CMakeSystem.cmake");
        if (!access(filename, F_OK)) {
            tool_write(filename, "set(CMAKE_SYSTEM_NAME \"Windows\")\n");
            systems++;
        }
    }
    REQUIRE(!closedir(files));
    REQUIRE(systems == 1);
    REQUIRE(inspect() == 0);
    yyjson_doc *doc = yyjson_read_file(log_path, 0, NULL, NULL);
    REQUIRE(doc);
    yyjson_val *result = yyjson_doc_get_root(doc);
    yyjson_val *compiled = yyjson_obj_get(result, "compiled");
    yyjson_val *tools = yyjson_obj_get(compiled, "private_executables");
    REQUIRE(yyjson_obj_size(tools) == 1);
    REQUIRE(!yyjson_obj_get(tools, "chibi-build-host"));
    yyjson_val *compiler = yyjson_obj_get(tools, "chibi-compiler-host");
    REQUIRE(text_is(compiler, "path", "chibi-compiler-host.exe"));
    const char *installed = "libexec/chibi/chibi-compiler-host.exe";
    REQUIRE(text_is(compiler, "install_path", installed));
    yyjson_val *private_tools = yyjson_obj_get(result, "private_tools");
    REQUIRE(yyjson_obj_size(private_tools) == 1);
    REQUIRE(text_is(private_tools, "chibi-compiler-host", installed));
    REQUIRE(!yyjson_obj_get(private_tools, "chibi-build-host"));
    yyjson_val *objects = yyjson_obj_get(compiled, "objects");
    REQUIRE(yyjson_obj_size(objects) == 2);
    REQUIRE(yyjson_obj_get(objects, "CMakeFiles/chibi_compiler_host.dir/src/host.c.o"));
    REQUIRE(compiler_archive(result));
    REQUIRE(yyjson_obj_size(yyjson_obj_get(compiled, "executables")) == 0);
    yyjson_doc_free(doc);

    path(filename, stage, installed);
    path(saved, directory, "saved-windows-compiler");
    REQUIRE(!rename(filename, saved));
    int status = inspect();
    REQUIRE(!rename(saved, filename));
    REQUIRE(diagnostic(status, "Installed private executable differs from compiled evidence:"));
    REQUIRE(!rename(filename, saved));
    tool_write(filename, "corrupt compiler executable\n");
    REQUIRE(!chmod(filename, 0700));
    status = inspect();
    REQUIRE(!unlink(filename) && !rename(saved, filename));
    REQUIRE(diagnostic(status, "Installed private executable differs from compiled evidence:"));

    path(filename, stage, "libexec/chibi/chibi-build-host");
    tool_write(filename, "unexpected POSIX-only tool\n");
    REQUIRE(!chmod(filename, 0700));
    status = inspect();
    REQUIRE(!unlink(filename));
    REQUIRE(
        diagnostic(status, "Installed private executables differ from the configured tool contract")
    );

    REQUIRE(missing_link_rejected(
        "CMakeFiles/chibi-compiler-host.dir/link.txt",
        "libchibi_compiler_host.a",
        "Compiler runtime archive has no installed executable consumer:"
    ));
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
    RUN_TEST_CASE(host_components_match_the_configured_platform);
    RUN_TEST_CASE(required_unavailable_host_component_names_the_component);
    RUN_TEST_CASE(configured_artifacts_preserve_complete_object_ownership);
#if defined(__linux__) || defined(__APPLE__)
    RUN_TEST_CASE(makefiles_tools_own_private_objects_and_installed_runtime);
    RUN_TEST_CASE(windows_private_compiler_owns_exe_without_a_posix_build_host);
#endif
    tool_remove_tree(directory);
    if (saved_archive_date) {
        assert(!setenv("ZERO_AR_DATE", saved_archive_date, 1));
    } else {
        assert(!unsetenv("ZERO_AR_DATE"));
    }
    free(saved_archive_date);
}
