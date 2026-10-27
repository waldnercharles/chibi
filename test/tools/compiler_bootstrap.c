#include "compiler_bootstrap.h"

#include "execution/function.h"
#include "execution/status.h"
#include "support/sha256.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"
#include "toolchain/compiler_bootstrap.h"
#include "toolchain/compiler_host.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The SHA-256 of "abc", from FIPS 180-2. */
#define ABC_SHA256                                                             \
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
/* A manifest's sources and target when the check stops before both. */
#define NOTHING "[]", "[]"
/* A target no host has. */
#define ELSEWHERE "[{\"pointer_size\": 4}, 0, [], [], [], []]"
/* How long a recovery may take: one or two compiles of the compiler. */
#define RECOVERY_SECONDS 50

static const char *host_path, *producer_path, *artifact_path;
static char root[PATH_MAX], log_path[PATH_MAX], compiler[PATH_MAX], core[PATH_MAX];
static char api_folder[PATH_MAX], api_sources[512], api_digest[65], api_error[512];
static unsigned char *api_bytes;
static size_t api_length;
static char *api_target;
static compiler_host *api_host;
static compiler_host_result api_result;
static const char api_source[] = "package api\n";
static const char api_core[] = "package core\n";

static void path(char *out, const char *relative)
{
    int length = snprintf(out, PATH_MAX, "%s/%s", root, relative);
    assert(length > 0 && length < PATH_MAX);
}

static void folder(const char *relative)
{
    char full[PATH_MAX];
    path(full, relative);
    assert(!mkdir(full, 0700));
}

static void write_file(const char *relative, const char *text)
{
    char full[PATH_MAX];
    path(full, relative);
    tool_write(full, text);
}

static void write_binary(const char *relative, const void *bytes, size_t length)
{
    char full[PATH_MAX];
    path(full, relative);
    FILE *file = fopen(full, "wb");
    assert(file && fwrite(bytes, 1, length, file) == length && !fclose(file));
}

static bool log_has(const char *text)
{
    char *output = tool_read(log_path);
    bool has = output && strstr(output, text);
    free(output);
    return has;
}

/* A file's bytes, or NULL. */
static unsigned char *read_bytes(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    unsigned char *bytes = NULL;
    long size = file && !fseek(file, 0, SEEK_END) ? ftell(file) : -1;
    if (size > 0 && !fseek(file, 0, SEEK_SET)) {
        bytes = xmalloc((size_t)size);
        if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
            free(bytes);
            bytes = NULL;
        }
    }
    if (file) {
        fclose(file);
    }
    *length = bytes ? (size_t)size : 0;
    return bytes;
}
/* The compiler sources' contract. */
static void contract(int version, int minimum)
{
    char text[128];
    snprintf(text, sizeof text, "version %d\nminimum %d\n", version, minimum);
    write_file("compiler/bootstrap.kdl", text);
}

/* The manifest of artifact folder name, recording format, version, a
 * closure of length bytes with digest, sources and target. */
static void manifest(
    const char *name,
    int format,
    int version,
    size_t length,
    const char *digest,
    const char *sources,
    const char *target
)
{
    char file[PATH_MAX];
    size_t capacity = strlen(digest) + strlen(sources) + strlen(target) + 512;
    char *text = xmalloc(capacity);
    int written = snprintf(
        text,
        capacity,
        "{\"format\": %d, \"version\": %d, \"minimum\": 1, \"revision\": \"\", "
        "\"closure\": {\"file\": \"compiler.chibip\", \"bytes\": %zu, "
        "\"sha256\": \"%s\"}, "
        "\"project\": \"/project\", \"core\": \"/sdk/core\", \"sources\": %s, "
        "\"target\": %s}\n",
        format,
        version,
        length,
        digest,
        sources,
        target
    );
    assert(written > 0 && (size_t)written < capacity);
    snprintf(file, sizeof file, "%s/bootstrap.json", name);
    write_file(file, text);
    free(text);
}

/* An artifact folder holding closure and its manifest. */
static void artifact(
    const char *name,
    int format,
    int version,
    const char *closure,
    const char *digest,
    const char *sources,
    const char *target
)
{
    char file[PATH_MAX];
    folder(name);
    snprintf(file, sizeof file, "%s/compiler.chibip", name);
    write_file(file, closure);
    manifest(name, format, version, strlen(closure), digest, sources, target);
}

/* The host's bootstrap check of the artifact folder name. */
static int check(const char *name)
{
    char artifact_folder[PATH_MAX];
    path(artifact_folder, name);
    const char *argv[] = { host_path,    "--bootstrap", artifact_folder,
                           "--compiler", compiler,      "--core",
                           core,         NULL };
    return tool_run(argv, root, log_path);
}

TEST_CASE(bootstrap_usage_rejects_mixed_and_missing_options)
{
    const char *no_core[] = { host_path,    "--bootstrap", root,
                              "--compiler", compiler,      NULL };
    REQUIRE(tool_run(no_core, root, log_path) == 1 && log_has("usage:"));
    const char *mixed[] = { host_path, "--bootstrap", root, "--compiler",
                            compiler,  "--core",      core, "--artifact",
                            "x",       NULL };
    REQUIRE(tool_run(mixed, root, log_path) == 1 && log_has("usage:"));
    const char *revision[] = { host_path, "--bootstrap", root, "--compiler",
                               compiler,  "--core",      core, "--revision",
                               "r",       NULL };
    REQUIRE(tool_run(revision, root, log_path) == 1 && log_has("usage:"));
    const char *seeded[] = { host_path, "--bootstrap", root, "--compiler",
                             compiler,  "--core",      core, "--seed",
                             root,      "--recover",   "x",  NULL };
    REQUIRE(tool_run(seeded, root, log_path) == 1 && log_has("usage:"));
    return true;
}

/* The sources' bootstrap.kdl must exist and hold a minimum from 1 to its
 * version. */
TEST_CASE(bootstrap_needs_the_sources_contract)
{
    artifact("plain", 1, 1, "abc", ABC_SHA256, NOTHING);
    REQUIRE(check("plain") == 1 && log_has("bootstrap.kdl: it cannot be read"));
    contract(1, 2);
    REQUIRE(check("plain") == 1 && log_has("needs a version and a minimum"));
    return true;
}

/* An artifact of another format, or older than the sources' minimum, is
 * refused before anything loads. */
TEST_CASE(bootstrap_rejects_unknown_formats_and_old_versions)
{
    contract(3, 2);
    artifact("future", 2, 3, "abc", ABC_SHA256, NOTHING);
    REQUIRE(check("future") == 1 && log_has("bootstrap.json has format 2; this host reads format 1"));
    artifact("old", 1, 1, "abc", ABC_SHA256, NOTHING);
    REQUIRE(
        check("old") == 1 &&
        log_has("bootstrap version 1, older than 2, the oldest these compiler sources support")
    );
    folder("empty");
    REQUIRE(check("empty") == 1 && log_has("holds no readable bootstrap.json"));
    return true;
}

/* The closure must be the one the manifest records, by size and SHA-256.
 * Closures recorded with their digests, FIPS 180-2's for "abc" and those of
 * 55, 56 and 64 a's, at and just past a block's room for the length, pass on
 * to reading the sources; one recorded with another digest stops first and
 * names its own. */
TEST_CASE(bootstrap_checks_the_recorded_closure)
{
    static const struct
    {
        int length;
        const char *digest;
    } vectors[] = {
        { 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" },
        { 56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" },
        { 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb" },
    };
    contract(1, 1);
    artifact("abc", 1, 1, "abc", ABC_SHA256, NOTHING);
    REQUIRE(check("abc") == 1 && log_has("does not record Compile's arguments"));
    for (size_t i = 0; i < sizeof vectors / sizeof *vectors; i++) {
        char name[16], closure[65] = { 0 };
        snprintf(name, sizeof name, "a%d", vectors[i].length);
        memset(closure, 'a', (size_t)vectors[i].length);
        artifact(name, 1, 1, closure, vectors[i].digest, NOTHING);
        REQUIRE(check(name) == 1 && log_has("does not record Compile's arguments"));
    }
    artifact("damaged", 1, 1, "abd", ABC_SHA256, NOTHING);
    REQUIRE(
        check("damaged") == 1 && log_has("compiler.chibip, 3 bytes with SHA-256 ") &&
        log_has(", is not the closure bootstrap.json records")
    );
    return true;
}

/* Every recorded source must be on disk, and every .chibi file in their
 * packages must be recorded: a package that gained one is another package. */
TEST_CASE(bootstrap_reads_every_recorded_source)
{
    contract(1, 1);
    folder("compiler/lib");
    write_file("compiler/lib/A.chibi", "package lib\n");
    artifact("missing", 1, 1, "abc", ABC_SHA256, "[{\"path\": \"lib/Missing.chibi\", \"sha256\": \"\"}]", ELSEWHERE);
    REQUIRE(check("missing") == 1 && log_has("cannot read the compiler source lib/Missing.chibi"));
    write_file("compiler/lib/B.chibi", "package lib\n");
    artifact("grown", 1, 1, "abc", ABC_SHA256, "[{\"path\": \"lib/A.chibi\", \"sha256\": \"\"}]", ELSEWHERE);
    REQUIRE(check("grown") == 1 && log_has("lib/B.chibi is not among the artifact's sources"));
    return true;
}

/* A closure this host loads, the compiler-artifact fixture's, recorded for
 * another target is refused before it compiles anything. */
TEST_CASE(bootstrap_rejects_another_target)
{
    char produced[PATH_MAX], closure[PATH_MAX];
    folder("produced");
    path(produced, "produced");
    const char *produce[] = { producer_path, "--compiler-artifact-write", produced, NULL };
    REQUIRE(tool_run_within(produce, root, log_path, 40) == 0);
    folder("other");
    path(closure, "other/compiler.chibip");
    char source[PATH_MAX];
    path(source, "produced/compiler.artifact");
    REQUIRE(!rename(source, closure));
    contract(1, 1);
    folder("compiler/one");
    write_file("compiler/one/A.chibi", "package one\n");
    const char *sources = "[{\"path\": \"one/A.chibi\", \"sha256\": \"\"}]";
    /* The checksum error names the closure's digest, which the next manifest
     * records. */
    struct stat info;
    REQUIRE(!stat(closure, &info));
    manifest("other", 1, 1, (size_t)info.st_size, "", sources, ELSEWHERE);
    REQUIRE(check("other") == 1);
    char *output = tool_read(log_path);
    const char *found = output ? strstr(output, "with SHA-256 ") : NULL;
    char digest[65] = { 0 };
    if (found) {
        memcpy(digest, found + strlen("with SHA-256 "), 64);
    }
    free(output);
    REQUIRE(found);
    manifest("other", 1, 1, (size_t)info.st_size, digest, sources, ELSEWHERE);
    REQUIRE(
        check("other") == 1 &&
        log_has("the artifact was compiled for other value layout than this host's")
    );
    return true;
}

/* Writing needs a seed whose sources are the ones on disk, and writes
 * nothing otherwise. */
TEST_CASE(bootstrap_write_checks_the_seed)
{
    contract(1, 1);
    folder("seed");
    char seed[PATH_MAX], out[PATH_MAX];
    path(seed, "seed");
    path(out, "written");
    const char *argv[] = { host_path, "--bootstrap", out,  "--compiler",
                           compiler,  "--core",      core, "--seed",
                           seed,      NULL };
    REQUIRE(tool_run(argv, root, log_path) == 1 && log_has("needs compiler.chibip and compile.json"));
    folder("compiler/two");
    write_file("compiler/two/A.chibi", "package two\n");
    write_file("seed/compiler.chibip", "abc");
    write_file(
        "seed/compile.json",
        "[\"/project\", [\"core\"], [\"/sdk/core\"], [\"two/A.chibi\"], "
        "[\"package other\\n\"], false, {}, 0, "
        "[], [], [], []]"
    );
    REQUIRE(
        tool_run(argv, root, log_path) == 1 &&
        log_has("the seed compiled two/A.chibi other than it is on disk")
    );
    /* A large seed must reach source validation, but never exceed the
     * artifact bound. No prepared decoding or compilation happens here. */
    const size_t lengths[] = { COMPILER_HOST_MAX_OUTPUT_BYTES + 1u,
                               COMPILER_HOST_MAX_ARTIFACT_BYTES,
                               COMPILER_HOST_MAX_ARTIFACT_BYTES + 1u };
    char closure[PATH_MAX];
    path(closure, "seed/compiler.chibip");
    for (int i = 0; i < 3; i++) {
        REQUIRE(!truncate(closure, (off_t)lengths[i]));
        REQUIRE(tool_run(argv, root, log_path) == 1);
        REQUIRE(log_has(
            i == 2 ? "needs compiler.chibip and compile.json"
                   : "the seed compiled two/A.chibi other than it is on disk"
        ));
    }
    struct stat info;
    REQUIRE(stat(out, &info) != 0);
    return true;
}

static void cleanup_load()
{
    compiler_host_free(api_host);
    api_host = NULL;
    free(api_result.json);
    api_result = (compiler_host_result){ 0 };
}

static compiler_host *load(const char *folder)
{
    compiler_bootstrap options = { .folder = folder, .compiler = compiler, .core = core };
    api_error[0] = 0;
    api_host = compiler_bootstrap_load(&options, api_error, sizeof api_error);
    return api_host;
}

static void api_manifest(const char *target)
{
    manifest("api-bootstrap", 1, 1, api_length, api_digest, api_sources, target);
}

/* A trusted prepared fixture has no CompileClosure export. Matching source
 * hashes must load it without trying to rebuild it. The returned host must
 * still execute after the loader's manifest/closure buffers retire. */
TEST_CASE(bootstrap_load_uses_matching_sources_without_recompilation)
{
    folder("api-produced");
    char produced[PATH_MAX], file[PATH_MAX];
    path(produced, "api-produced");
    const char *produce[] = { producer_path, "--compiler-artifact-write", produced, NULL };
    REQUIRE(tool_run_within(produce, root, log_path, 40) == 0);
    path(file, "api-produced/compiler.artifact");
    api_bytes = read_bytes(file, &api_length);
    REQUIRE(api_bytes && api_length);
    sha256_hex(api_bytes, api_length, api_digest);
    api_host = compiler_host_load(api_bytes, api_length, BEHAVIOR_BYTECODE, api_error, sizeof api_error);
    REQUIRE(api_host);
    size_t target_length = 0;
    api_target = compiler_host_target_json(api_host, &target_length);
    REQUIRE(api_target && target_length);
    cleanup_load();
    folder("compiler/api");
    write_file("compiler/api/Fixture.chibi", api_source);
    write_file("core/API.chibi", api_core);
    char source_digest[65], core_digest[65];
    sha256_hex(api_source, sizeof api_source - 1, source_digest);
    sha256_hex(api_core, sizeof api_core - 1, core_digest);
    snprintf(
        api_sources,
        sizeof api_sources,
        "[{\"path\":\"api/Fixture.chibi\",\"sha256\":\"%s\"},"
        "{\"path\":\"/sdk/core/API.chibi\",\"sha256\":\"%s\"}]",
        source_digest,
        core_digest
    );
    contract(1, 1);
    folder("api-bootstrap");
    path(api_folder, "api-bootstrap");
    write_binary("api-bootstrap/compiler.chibip", api_bytes, api_length);
    api_manifest(api_target);
    REQUIRE(load(api_folder));
    path(file, "api-produced/entry.txt");
    char *id = tool_read(file);
    REQUIRE(id);
    uint64_t entry = strtoull(id, NULL, 16);
    free(id);
    path(file, "api-produced/requests.json");
    char *arguments = tool_read(file);
    REQUIRE(arguments);
    bool called = compiler_host_invoke_batch(api_host, entry, arguments, strlen(arguments), 3000000, &api_result);
    free(arguments);
    REQUIRE(called && api_result.flow_status == FLOW_OK);
    path(file, "api-produced/expected.json");
    char *expected = tool_read(file);
    bool same = expected && api_result.json && !strcmp(expected, api_result.json);
    free(expected);
    REQUIRE(same);
    return true;
}

TEST_CASE(bootstrap_load_rejects_damaged_artifacts_and_old_versions)
{
    REQUIRE(api_bytes && api_target);
    unsigned char first = api_bytes[0];
    api_bytes[0] ^= 1;
    write_binary("api-bootstrap/compiler.chibip", api_bytes, api_length);
    api_bytes[0] = first;
    REQUIRE(!load(api_folder));
    REQUIRE(strstr(api_error, "is not the closure bootstrap.json records"));
    write_binary("api-bootstrap/compiler.chibip", api_bytes, api_length);
    manifest("api-bootstrap", 2, 1, api_length, api_digest, api_sources, api_target);
    REQUIRE(!load(api_folder));
    REQUIRE(strstr(api_error, "has format 2; this host reads format 1"));
    contract(3, 2);
    api_manifest(api_target);
    REQUIRE(!load(api_folder));
    REQUIRE(strstr(api_error, "bootstrap version 1, older than 2"));
    contract(1, 1);
    return true;
}

/* Sparse invalid closures exercise the read bound without preparing a large
 * program. The digest diagnostic reports the bytes actually admitted; all
 * accepted reads must still pass checksum and prepared validation. */
TEST_CASE(bootstrap_load_reads_closures_up_to_the_artifact_bound)
{
    REQUIRE(api_bytes && api_target);
    const size_t lengths[] = { COMPILER_HOST_MAX_OUTPUT_BYTES,
                               COMPILER_HOST_MAX_OUTPUT_BYTES + 1u,
                               COMPILER_HOST_MAX_ARTIFACT_BYTES,
                               COMPILER_HOST_MAX_ARTIFACT_BYTES + 1u };
    char file[PATH_MAX], expected[64];
    path(file, "api-bootstrap/compiler.chibip");
    for (int i = 0; i < 4; i++) {
        write_binary("api-bootstrap/compiler.chibip", "", 0);
        REQUIRE(!truncate(file, (off_t)lengths[i]));
        REQUIRE(!load(api_folder));
        snprintf(expected, sizeof expected, "compiler.chibip, %zu bytes", i == 3 ? 0 : lengths[i]);
        REQUIRE(strstr(api_error, expected));
        REQUIRE(strstr(api_error, "is not the closure bootstrap.json records"));
    }
    write_binary("api-bootstrap/compiler.chibip", api_bytes, api_length);
    REQUIRE(load(api_folder));
    return true;
}

TEST_CASE(bootstrap_load_rejects_another_prepared_abi)
{
    REQUIRE(api_target);
    yyjson_doc *original = yyjson_read(api_target, strlen(api_target), 0);
    REQUIRE(original);
    yyjson_mut_doc *changed = yyjson_doc_mut_copy(original, NULL);
    yyjson_doc_free(original);
    REQUIRE(changed);
    yyjson_mut_val *target = yyjson_mut_doc_get_root(changed);
    yyjson_mut_val *abi = yyjson_mut_arr_get(target, 1);
    bool replaced = yyjson_mut_arr_replace(
                        target,
                        1,
                        yyjson_mut_uint(changed, yyjson_mut_get_uint(abi) + 1)
                    ) != NULL;
    char *text = replaced ? yyjson_mut_write(changed, 0, NULL) : NULL;
    REQUIRE(text);
    api_manifest(text);
    free(text);
    yyjson_mut_doc_free(changed);
    REQUIRE(!load(api_folder));
    REQUIRE(strstr(api_error, "other prepared ABI than this host's"));
    api_manifest(api_target);
    return true;
}

TEST_CASE(bootstrap_load_checks_compiler_and_core_source_completeness)
{
    REQUIRE(api_target);
    write_file("compiler/api/Extra.chibi", "package api\n");
    REQUIRE(!load(api_folder));
    REQUIRE(strstr(api_error, "api/Extra.chibi is not among the artifact's sources"));
    char file[PATH_MAX];
    path(file, "compiler/api/Extra.chibi");
    REQUIRE(!unlink(file));
    write_file("core/Extra.chibi", "package core\n");
    REQUIRE(!load(api_folder));
    REQUIRE(strstr(api_error, "/sdk/core/Extra.chibi is not among the artifact's sources"));
    path(file, "core/Extra.chibi");
    REQUIRE(!unlink(file));
    path(file, "compiler/api/Fixture.chibi");
    REQUIRE(!unlink(file));
    REQUIRE(!load(api_folder));
    REQUIRE(strstr(api_error, "cannot read the compiler source api/Fixture.chibi"));
    write_file("compiler/api/Fixture.chibi", api_source);
    return true;
}

/* Hashing must compare current bytes on every load, for compiler and core
 * sources. This fixture cannot recover, so any changed source must reject
 * through CompileClosure, then load successfully once restored. */
TEST_CASE(bootstrap_load_does_not_cache_source_hashes)
{
    REQUIRE(api_target && api_bytes && api_folder[0]);
    const char *files[] = { "compiler/api/Fixture.chibi", "core/API.chibi" };
    const char *sources[] = { api_source, api_core };
    for (int i = 0; i < 2; i++) {
        char changed[32];
        snprintf(changed, sizeof changed, "%s", sources[i]);
        changed[8] ^= 1;
        write_file(files[i], changed);
        REQUIRE(!load(api_folder));
        REQUIRE(strstr(api_error, "the compiler failed:"));
        write_file(files[i], sources[i]);
        REQUIRE(load(api_folder));
        cleanup_load();
    }
    return true;
}

static char reuse_folder[PATH_MAX], reuse_compiler[PATH_MAX],
    reuse_core[PATH_MAX], reuse_manifest_path[PATH_MAX];
static unsigned char *reuse_bytes;
static size_t reuse_length;
static char *reuse_manifest_text;

static yyjson_mut_doc *reuse_input()
{
    yyjson_doc *manifest = yyjson_read(reuse_manifest_text, strlen(reuse_manifest_text), 0);
    assert(manifest);
    yyjson_val *record = yyjson_doc_get_root(manifest);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *args = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, args);
    yyjson_mut_arr_append(args, yyjson_val_mut_copy(doc, yyjson_obj_get(record, "project")));
    yyjson_mut_arr_add_str(doc, yyjson_mut_arr_add_arr(doc, args), "core");
    yyjson_mut_arr_append(
        yyjson_mut_arr_add_arr(doc, args),
        yyjson_val_mut_copy(doc, yyjson_obj_get(record, "core"))
    );
    yyjson_mut_val *paths = yyjson_mut_arr_add_arr(doc, args),
                   *texts = yyjson_mut_arr_add_arr(doc, args);
    yyjson_val *sources = yyjson_obj_get(record, "sources");
    for (size_t i = 0; i < yyjson_arr_size(sources); i++) {
        const char *name = yyjson_get_str(
            yyjson_obj_get(yyjson_arr_get(sources, i), "path")
        );
        char file[4096];
        if (!strncmp(name, "/sdk/core/", 10)) {
            snprintf(file, sizeof file, "%s/%s", reuse_core, name + 10);
        } else {
            snprintf(file, sizeof file, "%s/%s", reuse_compiler, name);
        }
        size_t length = 0;
        unsigned char *text = read_bytes(file, &length);
        assert(text && length);
        yyjson_mut_arr_add_strcpy(doc, paths, name);
        yyjson_mut_arr_add_strncpy(doc, texts, (const char *)text, length);
        free(text);
    }
    yyjson_mut_arr_add_false(doc, args);
    yyjson_val *target = yyjson_obj_get(record, "target");
    for (size_t i = 0; i < 6; i++) {
        yyjson_mut_arr_append(args, yyjson_val_mut_copy(doc, yyjson_arr_get(target, i)));
    }
    yyjson_doc_free(manifest);
    return doc;
}

/* Reuse loads and checks the closure, several seconds for the 13 MB package
 * compiler under the sanitizers; recompiling it takes minutes. */
#define REUSE_SECONDS 30

static int reuse_request(yyjson_mut_doc *doc, const char *output)
{
    char request[PATH_MAX];
    path(request, "reuse-input.json");
    char *text = yyjson_mut_write(doc, 0, NULL);
    assert(text);
    tool_write(request, text);
    free(text);
    const char *argv[] = { host_path,      "--reuse-bootstrap",
                           reuse_folder,   "--compiler",
                           reuse_compiler, "--core",
                           reuse_core,     "--input",
                           request,        "--output-artifact",
                           output,         NULL };
    return tool_run_within(argv, root, log_path, REUSE_SECONDS);
}

static bool output_is(const char *output, const void *expected, size_t expected_length)
{
    size_t length = 0;
    unsigned char *bytes = read_bytes(output, &length);
    bool matches = bytes && length == expected_length &&
                   !memcmp(bytes, expected, length);
    free(bytes);
    REQUIRE(matches);
    return true;
}

/* The canonical compiler entry and its captured request are reused, not run.
 * Copying retires the CLI's owned source, artifact and decoded host buffers. */
TEST_CASE(bootstrap_reuses_only_the_exact_captured_request_without_compilation)
{
    folder("reuse-real");
    char archive[PATH_MAX], extracted[PATH_MAX], closure[PATH_MAX];
    path(archive, "reuse-sources.tar");
    path(extracted, "reuse-real");
    const char *export[] = {
        "git",      "-C",    tool_root(), "archive",        "--format=tar",
        "--output", archive, "HEAD",      "tools/compiler", "packages/core",
        NULL
    };
    REQUIRE(tool_run(export, root, log_path) == 0);
    const char *extract[] = { "tar", "-xf", archive, "-C", extracted, NULL };
    REQUIRE(tool_run(extract, root, log_path) == 0);
    path(reuse_folder, "reuse-real/tools/compiler/bootstrap");
    path(reuse_compiler, "reuse-real/tools/compiler");
    path(reuse_core, "reuse-real/packages/core");
    path(reuse_manifest_path, "reuse-real/tools/compiler/bootstrap/bootstrap.json");
    path(closure, "reuse-real/tools/compiler/bootstrap/compiler.chibip");
    reuse_manifest_text = tool_read(reuse_manifest_path);
    reuse_bytes = read_bytes(closure, &reuse_length);
    REQUIRE(reuse_manifest_text && reuse_bytes && reuse_length);
    char output[PATH_MAX], request[PATH_MAX];
    path(output, "reused.chibip");
    path(request, "reuse-input.json");
    tool_write(output, "prior output");
    yyjson_mut_doc *doc = reuse_input();
    int status = reuse_request(doc, output);
    yyjson_mut_doc_free(doc);
    REQUIRE(status == 0);
    char *log = tool_read(log_path);
    bool silent = log && !*log;
    free(log);
    REQUIRE(silent);
    REQUIRE(!log_has("compiler-host bytecode="));
    REQUIRE(!unlink(request));
    REQUIRE(output_is(output, reuse_bytes, reuse_length));

    /* Run counters nest exclusively inside compiler_host, so an outer run
     * counter cannot forbid compilation here. A pure instruction scope is
     * shared instead. Calibrate it against this exact real CompileClosure
     * request: even one canonical instruction must debit the caller's meter. */
    compiler_bootstrap bootstrap = { .folder = reuse_folder,
                                     .compiler = reuse_compiler,
                                     .core = reuse_core };
    char why[512];
    compiler_host *loaded = compiler_bootstrap_load(&bootstrap, why, sizeof why);
    REQUIRE(loaded);
    doc = reuse_input();
    size_t length = 0;
    char *json = yyjson_mut_write(doc, 0, &length);
    yyjson_mut_doc_free(doc);
    REQUIRE(json && length);
    compiler_host_bytes attempted = { 0 };
    behavior_execution_scope calibration = { .remaining = 1, .pure = true };
    behavior_execution_scope_begin(&calibration);
    bool called = compiler_host_invoke_bytes(
        loaded,
        compiler_host_entry("project:package_compiler:1:CompileClosure:callable"),
        json,
        length,
        1,
        COMPILER_HOST_MAX_OUTPUT_BYTES,
        &attempted
    );
    behavior_execution_scope_end(&calibration);
    compiler_host_free(loaded);
    bool no_output = !attempted.bytes && !attempted.length;
    free(attempted.bytes);

    /* Reuse must consume zero instructions, not merely return identical bytes
     * quietly. An attempted compilation exhausts this one-step sentinel even
     * if its error is swallowed before copying the recorded bytes anyway. */
    behavior_execution_scope forbidden = { .remaining = 1, .pure = true };
    behavior_execution_scope_begin(&forbidden);
    compiler_bootstrap_reuse_result
        reused = compiler_bootstrap_reuse(&bootstrap, json, length, output, why, sizeof why);
    behavior_execution_scope_end(&forbidden);
    free(json);
    REQUIRE(!called && attempted.flow_status == FLOW_ERR_STEP_BUDGET);
    REQUIRE(calibration.remaining == 0 && attempted.counts.bytecode_entries > 0);
    REQUIRE(strstr(attempted.diagnostic, "shared instruction budget exhausted"));
    REQUIRE(no_output);
    REQUIRE(reused == COMPILER_BOOTSTRAP_REUSED);
    REQUIRE(forbidden.remaining == 1);
    REQUIRE(!behavior_execution_scope_active());
    REQUIRE(output_is(output, reuse_bytes, reuse_length));
    return true;
}

TEST_CASE(bootstrap_reuse_preserves_output_for_changed_captured_requests)
{
    REQUIRE(reuse_manifest_text && reuse_bytes);
    static const char prior[] = "prior complete output";
    char output[PATH_MAX];
    path(output, "different.chibip");
    for (int variant = 0; variant < 10; variant++) {
        yyjson_mut_doc *doc = reuse_input();
        yyjson_mut_val *args = yyjson_mut_doc_get_root(doc);
        yyjson_mut_val *paths = yyjson_mut_arr_get(args, 3),
                       *texts = yyjson_mut_arr_get(args, 4);
        switch (variant) {
            case 0:
                yyjson_mut_arr_replace(texts, 0, yyjson_mut_str(doc, "package package_compiler\n// changed snapshot\n"));
                break;
            case 1: {
                uint64_t abi = yyjson_mut_get_uint(yyjson_mut_arr_get(args, 7));
                yyjson_mut_arr_replace(args, 7, yyjson_mut_uint(doc, abi + 1));
                break;
            }
            case 2:
                yyjson_mut_arr_replace(paths, 0, yyjson_mut_str(doc, "package_compiler/Renamed.chibi"));
                break;
            case 3:
                yyjson_mut_arr_replace(args, 0, yyjson_mut_str(doc, "/elsewhere"));
                break;
            case 4:
                yyjson_mut_arr_replace(args, 5, yyjson_mut_bool(doc, true));
                break;
            case 5: {
                yyjson_mut_val *p0 = yyjson_mut_val_mut_copy(doc, yyjson_mut_arr_get(paths, 0));
                yyjson_mut_val *p1 = yyjson_mut_val_mut_copy(doc, yyjson_mut_arr_get(paths, 1));
                yyjson_mut_val *t0 = yyjson_mut_val_mut_copy(doc, yyjson_mut_arr_get(texts, 0));
                yyjson_mut_val *t1 = yyjson_mut_val_mut_copy(doc, yyjson_mut_arr_get(texts, 1));
                yyjson_mut_arr_replace(paths, 0, p1);
                yyjson_mut_arr_replace(paths, 1, p0);
                yyjson_mut_arr_replace(texts, 0, t1);
                yyjson_mut_arr_replace(texts, 1, t0);
                break;
            }
            case 6:
                yyjson_mut_arr_add_str(doc, yyjson_mut_arr_get(args, 1), "game");
                yyjson_mut_arr_add_str(doc, yyjson_mut_arr_get(args, 2), "/sdk/game");
                break;
            case 7:
                yyjson_mut_arr_add_str(doc, paths, "package_compiler/Extra.chibi");
                yyjson_mut_arr_add_str(doc, texts, "package package_compiler\n");
                break;
            case 8:
                yyjson_mut_arr_remove(paths, 1);
                yyjson_mut_arr_remove(texts, 1);
                break;
            case 9: {
                yyjson_mut_val *p0 = yyjson_mut_val_mut_copy(doc, yyjson_mut_arr_get(paths, 0));
                yyjson_mut_val *t0 = yyjson_mut_val_mut_copy(doc, yyjson_mut_arr_get(texts, 0));
                yyjson_mut_arr_replace(paths, 1, p0);
                yyjson_mut_arr_replace(texts, 1, t0);
                break;
            }
        }
        tool_write(output, prior);
        int status = reuse_request(doc, output);
        yyjson_mut_doc_free(doc);
        REQUIRE(status == 3);
        REQUIRE(output_is(output, prior, sizeof prior - 1));
        REQUIRE(!log_has("reused the validated bootstrap closure"));
    }
    return true;
}

TEST_CASE(bootstrap_reuse_invalidates_changed_disk_sources_without_recovery)
{
    REQUIRE(reuse_manifest_text && reuse_bytes);
    static const char prior[] = "prior complete output";
    char output[PATH_MAX];
    path(output, "changed-disk.chibip");
    const char *names[] = {
        "reuse-real/tools/compiler/package_compiler/Compile.chibi",
        "reuse-real/packages/core/strings/Strings.chibi"
    };
    for (int i = 0; i < 2; i++) {
        char file[PATH_MAX];
        path(file, names[i]);
        size_t length = 0;
        unsigned char *original = read_bytes(file, &length);
        REQUIRE(original && length);
        yyjson_mut_doc *doc = reuse_input();
        tool_write(output, prior);
        write_file(names[i], "package changed\n");
        int status = reuse_request(doc, output);
        write_binary(names[i], original, length);
        free(original);
        yyjson_mut_doc_free(doc);
        REQUIRE(status == 3);
        REQUIRE(output_is(output, prior, sizeof prior - 1));
        REQUIRE(!log_has("compiler-host bytecode="));
    }
    return true;
}

static void reuse_manifest_field(const char *name, const char *value)
{
    yyjson_doc *original = yyjson_read(reuse_manifest_text, strlen(reuse_manifest_text), 0);
    assert(original);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(original, NULL);
    yyjson_doc_free(original);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_val *field = !strcmp(name, "format") ? yyjson_mut_uint(doc, 2)
                                                    : yyjson_mut_str(doc, value);
    yyjson_mut_obj_put(root, yyjson_mut_str(doc, name), field);
    char *text = yyjson_mut_write(doc, 0, NULL);
    assert(text);
    tool_write(reuse_manifest_path, text);
    free(text);
    yyjson_mut_doc_free(doc);
}

TEST_CASE(bootstrap_reuse_hard_validation_and_write_errors_preserve_output)
{
    REQUIRE(reuse_manifest_text && reuse_bytes);
    static const char prior[] = "prior complete output";
    char output[PATH_MAX], missing[PATH_MAX];
    path(output, "invalid.chibip");
    path(missing, "reuse-missing-parent/output.chibip");
    yyjson_mut_doc *doc = reuse_input();
    tool_write(output, prior);
    unsigned char first = reuse_bytes[0];
    reuse_bytes[0] ^= 1;
    write_binary("reuse-real/tools/compiler/bootstrap/compiler.chibip", reuse_bytes, reuse_length);
    reuse_bytes[0] = first;
    int status = reuse_request(doc, output);
    write_binary("reuse-real/tools/compiler/bootstrap/compiler.chibip", reuse_bytes, reuse_length);
    REQUIRE(status == 1 && log_has("is not the closure bootstrap.json records"));
    REQUIRE(output_is(output, prior, sizeof prior - 1));
    reuse_manifest_field("format", "");
    status = reuse_request(doc, output);
    tool_write(reuse_manifest_path, reuse_manifest_text);
    REQUIRE(status == 1 && log_has("has format 2; this host reads format 1"));
    REQUIRE(output_is(output, prior, sizeof prior - 1));
    reuse_manifest_field("entry", "0x0000000000000001");
    status = reuse_request(doc, output);
    tool_write(reuse_manifest_path, reuse_manifest_text);
    REQUIRE(status == 1 && log_has("the artifact records another compiler entry"));
    REQUIRE(output_is(output, prior, sizeof prior - 1));
    write_file("reuse-real/tools/compiler/package_compiler/Extra.chibi", "package package_compiler\n");
    status = reuse_request(doc, output);
    char extra[PATH_MAX];
    path(extra, "reuse-real/tools/compiler/package_compiler/Extra.chibi");
    REQUIRE(!unlink(extra));
    REQUIRE(status == 1 && log_has("is not among the artifact's sources"));
    REQUIRE(output_is(output, prior, sizeof prior - 1));
    REQUIRE(reuse_request(doc, missing) == 1 && log_has("cannot be written"));
    REQUIRE(access(missing, F_OK) != 0);
    REQUIRE(output_is(output, prior, sizeof prior - 1));
    yyjson_mut_doc_free(doc);
    return true;
}

TEST_CASE(bootstrap_reuse_usage_is_separate_from_legacy_modes)
{
    char input[PATH_MAX], output[PATH_MAX];
    path(input, "reuse-input.json");
    path(output, "usage-output.chibip");
    const char *keys[] = { "--bootstrap", "--artifact", "--entry",
                           "--backend",   "--budget",   "--timeout-ms",
                           "--seed",      "--recover",  "--revision" };
    const char *values[] = { reuse_folder, "x", "1", "sljit", "1",
                             "1",          "x", "x", "x" };
    for (int i = 0; i < (int)(sizeof keys / sizeof *keys); i++) {
        const char *argv[] = { host_path,      "--reuse-bootstrap",
                               reuse_folder,   "--compiler",
                               reuse_compiler, "--core",
                               reuse_core,     "--input",
                               input,          "--output-artifact",
                               output,         keys[i],
                               values[i],      NULL };
        REQUIRE(tool_run(argv, root, log_path) == 1 && log_has("usage:"));
        REQUIRE(access(output, F_OK) != 0);
    }
    const char *missing[] = { host_path,      "--reuse-bootstrap",
                              reuse_folder,   "--compiler",
                              reuse_compiler, "--core",
                              reuse_core,     NULL };
    REQUIRE(tool_run(missing, root, log_path) == 1 && log_has("usage:"));
    const char *raw[] = { host_path,      "--reuse-bootstrap",
                          reuse_folder,   "--compiler",
                          reuse_compiler, "--core",
                          reuse_core,     "--input",
                          input,          "--output-bytes",
                          output,         NULL };
    REQUIRE(tool_run(raw, root, log_path) == 1 && log_has("usage:"));
    REQUIRE(access(output, F_OK) != 0);
    return true;
}

static void setup_root()
{
    const char *temporary = getenv("TMPDIR");
    int length = snprintf(
        root,
        sizeof root,
        "%s/chibi-bootstrap-XXXXXX",
        temporary && *temporary ? temporary : "/tmp"
    );
    assert(length > 0 && (size_t)length < sizeof root);
    assert(mkdtemp(root));
    path(log_path, "host.log");
    path(compiler, "compiler");
    path(core, "core");
}

void test_tool_compiler_bootstrap(const char *producer, const char *host)
{
    producer_path = producer;
    host_path = host;
    setup_root();
    folder("compiler");
    folder("core");
    RUN_TEST_CASE(bootstrap_usage_rejects_mixed_and_missing_options);
    RUN_TEST_CASE(bootstrap_needs_the_sources_contract);
    RUN_TEST_CASE(bootstrap_rejects_unknown_formats_and_old_versions);
    RUN_TEST_CASE(bootstrap_checks_the_recorded_closure);
    RUN_TEST_CASE(bootstrap_reads_every_recorded_source);
    RUN_TEST_CASE(bootstrap_rejects_another_target);
    RUN_TEST_CASE(bootstrap_write_checks_the_seed);
    pu_setup(NULL, cleanup_load);
    RUN_TEST_CASE(bootstrap_load_uses_matching_sources_without_recompilation);
    RUN_TEST_CASE(bootstrap_load_rejects_damaged_artifacts_and_old_versions);
    RUN_TEST_CASE(bootstrap_load_reads_closures_up_to_the_artifact_bound);
    RUN_TEST_CASE(bootstrap_load_rejects_another_prepared_abi);
    RUN_TEST_CASE(bootstrap_load_checks_compiler_and_core_source_completeness);
    RUN_TEST_CASE(bootstrap_load_does_not_cache_source_hashes);
    RUN_TEST_CASE(bootstrap_reuses_only_the_exact_captured_request_without_compilation);
    RUN_TEST_CASE(bootstrap_reuse_preserves_output_for_changed_captured_requests);
    RUN_TEST_CASE(bootstrap_reuse_invalidates_changed_disk_sources_without_recovery);
    RUN_TEST_CASE(bootstrap_reuse_hard_validation_and_write_errors_preserve_output);
    RUN_TEST_CASE(bootstrap_reuse_usage_is_separate_from_legacy_modes);
    pu_clear_setup();
    free(reuse_manifest_text);
    reuse_manifest_text = NULL;
    free(reuse_bytes);
    reuse_bytes = NULL;
    free(api_target);
    api_target = NULL;
    free(api_bytes);
    api_bytes = NULL;
    tool_remove_tree(root);
}

/* The compiler sources and core collection of HEAD, exported by git rather
 * than taken from the working tree, recover the compiler from the artifact:
 * its own bytes when it was written from them, otherwise a compiler that
 * compiles them to itself. --recover writes that compiler. */
TEST_CASE(bootstrap_recovers_from_a_clean_checkout)
{
    char archive[PATH_MAX];
    path(archive, "sources.tar");
    const char *export[] = {
        "git",      "-C",    tool_root(), "archive",        "--format=tar",
        "--output", archive, "HEAD",      "tools/compiler", "packages/core",
        NULL
    };
    REQUIRE(tool_run(export, root, log_path) == 0);
    const char *extract[] = { "tar", "-xf", archive, "-C", root, NULL };
    REQUIRE(tool_run(extract, root, log_path) == 0);
    path(compiler, "tools/compiler");
    path(core, "packages/core");
    char recovered[PATH_MAX], closure[4096];
    path(recovered, "recovered.chibip");
    snprintf(closure, sizeof closure, "%s/compiler.chibip", artifact_path);
    const char *argv[] = { host_path,    "--bootstrap", artifact_path,
                           "--compiler", compiler,      "--core",
                           core,         "--recover",   recovered,
                           NULL };
    REQUIRE(tool_run_within(argv, root, log_path, RECOVERY_SECONDS) == 0);
    bool own = log_has("recovered the compiler of revision ");
    REQUIRE(own || log_has("that compiles them to itself"));
    size_t length = 0, expected = 0;
    unsigned char *bytes = read_bytes(recovered, &length);
    unsigned char *original = read_bytes(closure, &expected);
    bool written = bytes && length &&
                   (!own || (original && length == expected &&
                             !memcmp(bytes, original, length)));
    free(bytes);
    free(original);
    REQUIRE(written);
    return true;
}

TEST_CASE(bootstrap_load_recovers_changed_sources_without_writing)
{
    char source[PATH_MAX], output[PATH_MAX];
    path(source, "tools/compiler/package_compiler/Compile.chibi");
    size_t length = 0;
    unsigned char *original = read_bytes(source, &length);
    REQUIRE(original && length);
    FILE *file = fopen(source, "ab");
    REQUIRE(file);
    bool changed = fputs("\npub BootstrapLoadMarker :: () -> int { return 37; }\n", file) >=
                   0;
    changed = !fclose(file) && changed;
    REQUIRE(changed);
    path(output, "load-must-not-write.chibip");
    compiler_bootstrap options = { .folder = artifact_path,
                                   .compiler = compiler,
                                   .core = core,
                                   .output = output };
    api_error[0] = 0;
    api_host = compiler_bootstrap_load(&options, api_error, sizeof api_error);
    file = fopen(source, "wb");
    assert(file && fwrite(original, 1, length, file) == length && !fclose(file));
    free(original);
    if (!api_host) {
        fprintf(stderr, "%s\n", api_error);
    }
    REQUIRE(api_host);
    REQUIRE(strstr(api_error, "compiler sources changed since revision"));
    REQUIRE(strstr(api_error, "that compiles them to itself"));
    const char *compiled = strstr(api_error, "compiled a compiler of ");
    size_t recovered = 0;
    REQUIRE(compiled && sscanf(compiled, "compiled a compiler of %zu bytes", &recovered) == 1);
    /* Interned strings keep the compiler near half the 16 MiB result bound,
     * and sources are capped at 8 MiB, so recovery can no longer be padded
     * past it. The artifact bound is covered by sparse closure reads and by
     * binary_artifact_bound_does_not_widen_ordinary_results. */
    REQUIRE(recovered > 0 && recovered <= COMPILER_HOST_MAX_ARTIFACT_BYTES);
    printf("%s\n", api_error);
    REQUIRE(access(output, F_OK) != 0);
    uint64_t entry = compiler_host_entry("project:package_compiler:1:BootstrapLoadMarker:callable");
    REQUIRE(compiler_host_invoke(api_host, entry, "[]", 2, 1000, &api_result));
    REQUIRE(api_result.flow_status == FLOW_OK);
    REQUIRE(api_result.json && !strcmp(api_result.json, "[37]"));
    return true;
}

void test_tool_bootstrap_recovery(const char *host, const char *artifact)
{
    host_path = host;
    artifact_path = artifact;
    setup_root();
    RUN_TEST_CASE(bootstrap_recovers_from_a_clean_checkout);
    pu_setup(NULL, cleanup_load);
    RUN_TEST_CASE(bootstrap_load_recovers_changed_sources_without_writing);
    pu_clear_setup();
    tool_remove_tree(root);
}
