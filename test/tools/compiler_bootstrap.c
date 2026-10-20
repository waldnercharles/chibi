#include "compiler_bootstrap.h"

#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
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
static char root[64], log_path[128], compiler[128], core[128];

static void path(char *out, const char *relative)
{
    int length = snprintf(out, 256, "%s/%s", root, relative);
    assert(length > 0 && length < 256);
}

static void folder(const char *relative)
{
    char full[256];
    path(full, relative);
    assert(!mkdir(full, 0700));
}

static void write_file(const char *relative, const char *text)
{
    char full[256];
    path(full, relative);
    tool_write(full, text);
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
    char file[256], text[2048];
    snprintf(
        text,
        sizeof text,
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
    snprintf(file, sizeof file, "%s/bootstrap.json", name);
    write_file(file, text);
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
    char file[256];
    folder(name);
    snprintf(file, sizeof file, "%s/compiler.chibip", name);
    write_file(file, closure);
    manifest(name, format, version, strlen(closure), digest, sources, target);
}

/* The host's bootstrap check of the artifact folder name. */
static int check(const char *name)
{
    char artifact_folder[256];
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
    char produced[256], closure[256];
    folder("produced");
    path(produced, "produced");
    const char *produce[] = { producer_path, "--compiler-artifact-write", produced, NULL };
    REQUIRE(tool_run_within(produce, root, log_path, 40) == 0);
    folder("other");
    path(closure, "other/compiler.chibip");
    char source[256];
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
    char seed[256], out[256];
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
        "[\"package other\\n\"], {}, 0, "
        "[], [], [], []]"
    );
    REQUIRE(
        tool_run(argv, root, log_path) == 1 &&
        log_has("the seed compiled two/A.chibi other than it is on disk")
    );
    struct stat info;
    REQUIRE(stat(out, &info) != 0);
    return true;
}

static void setup_root()
{
    strcpy(root, "/tmp/chibi-bootstrap-XXXXXX");
    assert(mkdtemp(root));
    snprintf(log_path, sizeof log_path, "%s/host.log", root);
    snprintf(compiler, sizeof compiler, "%s/compiler", root);
    snprintf(core, sizeof core, "%s/core", root);
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
    tool_remove_tree(root);
}

/* The compiler sources and core collection of HEAD, exported by git rather
 * than taken from the working tree, recover the compiler from the artifact:
 * its own bytes when it was written from them, otherwise a compiler that
 * compiles them to itself. --recover writes that compiler. */
TEST_CASE(bootstrap_recovers_from_a_clean_checkout)
{
    char archive[256];
    path(archive, "sources.tar");
    const char *export[] = {
        "git",      "-C",    tool_root(), "archive",        "--format=tar",
        "--output", archive, "HEAD",      "tools/compiler", "packages/core",
        NULL
    };
    REQUIRE(tool_run(export, root, log_path) == 0);
    const char *extract[] = { "tar", "-xf", archive, "-C", root, NULL };
    REQUIRE(tool_run(extract, root, log_path) == 0);
    snprintf(compiler, sizeof compiler, "%s/tools/compiler", root);
    snprintf(core, sizeof core, "%s/packages/core", root);
    char recovered[256], closure[4096];
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

void test_tool_bootstrap_recovery(const char *host, const char *artifact)
{
    host_path = host;
    artifact_path = artifact;
    setup_root();
    RUN_TEST_CASE(bootstrap_recovers_from_a_clean_checkout);
    tool_remove_tree(root);
}
