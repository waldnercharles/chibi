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
    REQUIRE(yyjson_obj_size(objects) == 7);
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

    /* Nine fixed compiled-evidence rejects, restoring each fixture mutation. */
    char filename[1024], saved[1024];
    path(filename, build, "CMakeFiles/folded_object.dir/src/object.c.o");
    path(saved, directory, "saved-object");
    REQUIRE(rename(filename, saved) == 0);
    int status = inspect();
    REQUIRE(rename(saved, filename) == 0);
    REQUIRE(diagnostic(status, "Build is incomplete:"));
    struct stat info;
    REQUIRE(stat(filename, &info) == 0);
    struct timespec times[] = { info.st_atim, info.st_mtim };
    struct timespec newer[] = { info.st_atim, info.st_mtim };
    newer[1].tv_sec += 60;
    REQUIRE(utimensat(AT_FDCWD, filename, newer, 0) == 0);
    status = inspect();
    REQUIRE(utimensat(AT_FDCWD, filename, times, 0) == 0);
    REQUIRE(diagnostic(status, "predates a link dependency"));

    char graph_path[1024];
    path(graph_path, build, "build.ninja");
    char *graph = tool_read(graph_path);
    REQUIRE(graph);
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
    times[0] = newer[0] = info.st_atim;
    times[1] = info.st_mtim;
    struct stat resource_info;
    path(saved, build, resource);
    REQUIRE(stat(saved, &resource_info) == 0);
    newer[1] = resource_info.st_mtim;
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
    return true;
}

void test_tool_package_ownership()
{
    strcpy(directory, "/tmp/chibi-package-ownership-XXXXXX");
    assert(mkdtemp(directory));
    path(build, directory, "build");
    path(stage, directory, "installed");
    path(source, tool_root(), "cmake/tests/package_ownership");
    path(log_path, directory, "child.log");
    RUN_TEST_CASE(configured_artifacts_preserve_complete_object_ownership);
    tool_remove_tree(directory);
}
