#include "host/package_identity.h"

#include "support/sha256.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[128], path[256];
static char error[256];
static package_identity identity;
static char *vector;
static const char MANIFEST[] = "{\"kind\":\"sdk\",\"build_id\":\"fixture-"
                               "build\",\"package_version\":\"9.2.1\"}\n";
static void setup()
{
    strcpy(root, "/tmp/chibi-package-identity-XXXXXX");
    assert(mkdtemp(root));
    snprintf(path, sizeof path, "%s/package.json", root);
    package_identity_init(&identity);
}
static void cleanup()
{
    package_identity_free(&identity);
    free(vector);
    vector = NULL;
    tool_remove_tree(root);
}
static void write_text(const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    size_t length = strlen(text);
    assert(fwrite(text, 1, length, file) == length);
    assert(!fclose(file));
}
static package_identity_status load()
{
    return package_identity_load(&identity, root, error, sizeof error);
}
static bool empty_identity()
{
    return !identity.version && !identity.build_id && !identity.manifest_sha256[0];
}
TEST_CASE(sha256_matches_standard_vectors_and_padding_boundaries)
{
    char hex[65];
    sha256_hex(NULL, 0, hex);
    REQUIRE(!strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    sha256_hex("abc", 3, hex);
    REQUIRE(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const char *long_vector = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmno"
                              "mnopnopq";
    sha256_hex(long_vector, strlen(long_vector), hex);
    REQUIRE(!strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    vector = xmalloc(1000000);
    memset(vector, 'a', 1000000);
    sha256_hex(vector, 1000000, hex);
    REQUIRE(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    unsigned char raw[32];
    sha256("abc", 3, raw);
    REQUIRE(raw[0] == 0xba && raw[1] == 0x78 && raw[31] == 0xad);
    return true;
}
TEST_CASE(package_identity_owns_sdk_fields_and_hashes_exact_manifest_bytes)
{
    write_text(MANIFEST);
    REQUIRE(load() == PACKAGE_IDENTITY_OK);
    REQUIRE(!error[0]);
    REQUIRE(!strcmp(identity.version, "9.2.1"));
    REQUIRE(!strcmp(identity.build_id, "fixture-build"));
    REQUIRE(
        !strcmp(identity.manifest_sha256, "0a766093dd4a062dde480f83a0713553e4d1205f94602b195bf15878dd6f017f")
    );
    char before[65];
    strcpy(before, identity.manifest_sha256);
    write_text(" { \"package_version\":\"9.2.1\", \"kind\":\"sdk\", \"build_id\":\"fixture-build\" }\n");
    REQUIRE(!strcmp(identity.version, "9.2.1")); /* Loaded strings survive file changes. */
    REQUIRE(load() == PACKAGE_IDENTITY_OK);
    REQUIRE(!strcmp(identity.version, "9.2.1") && !strcmp(identity.build_id, "fixture-build"));
    REQUIRE(strcmp(before, identity.manifest_sha256));
    write_text("{\"kind\":\"web-author\",\"build_id\":\"browser-build\",\"package_version\":\"9.2.1\"}\n");
    REQUIRE(load() == PACKAGE_IDENTITY_OK);
    REQUIRE(!strcmp(identity.build_id, "browser-build"));
    return true;
}
TEST_CASE(package_identity_invalid_manifests_never_retain_previous_identity)
{
    const char *invalid[] = {
        "",
        "{",
        "[]",
        "{}",
        "{\"kind\":\"runtime\",\"build_id\":\"id\",\"package_version\":\"1\"}",
        "{\"kind\":\"sdk\",\"build_id\":\"id\",\"package_version\":1}",
        "{\"kind\":\"sdk\",\"build_id\":\"\",\"package_version\":\"1\"}",
        "{\"kind\":\"sdk\",\"build_id\":\" \",\"package_version\":\"1\"}",
        "{\"kind\":\"sdk\",\"build_id\":\"a\\u0000b\",\"package_version\":"
        "\"1\"}",
        "{\"kind\":\"sdk\",\"kind\":\"runtime\",\"build_id\":\"id\",\"package_"
        "version\":\"1\"}",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        write_text(MANIFEST);
        REQUIRE(load() == PACKAGE_IDENTITY_OK);
        write_text(invalid[i]);
        REQUIRE(load() == PACKAGE_IDENTITY_INVALID);
        REQUIRE(error[0] && empty_identity());
    }
    return true;
}
TEST_CASE(package_identity_missing_manifest_is_explicitly_unavailable)
{
    REQUIRE(load() == PACKAGE_IDENTITY_UNAVAILABLE);
    REQUIRE(error[0] && empty_identity());
    write_text(MANIFEST);
    REQUIRE(load() == PACKAGE_IDENTITY_OK);
    REQUIRE(!unlink(path));
    REQUIRE(load() == PACKAGE_IDENTITY_UNAVAILABLE);
    REQUIRE(error[0] && empty_identity());
    return true;
}
TEST_CASE(package_identity_rejects_oversized_and_nonregular_files_without_reading_them)
{
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    REQUIRE(!fseek(file, PACKAGE_IDENTITY_MANIFEST_MAX, SEEK_SET));
    REQUIRE(fputc('x', file) == 'x');
    REQUIRE(!fclose(file));
    REQUIRE(load() == PACKAGE_IDENTITY_INVALID);
    REQUIRE(error[0] && empty_identity());
    REQUIRE(!unlink(path));
    REQUIRE(!mkfifo(path, 0600));
    REQUIRE(load() == PACKAGE_IDENTITY_INVALID);
    REQUIRE(error[0] && empty_identity());
    return true;
}
void test_package_identity()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(sha256_matches_standard_vectors_and_padding_boundaries);
    RUN_TEST_CASE(package_identity_owns_sdk_fields_and_hashes_exact_manifest_bytes);
    RUN_TEST_CASE(package_identity_invalid_manifests_never_retain_previous_identity);
    RUN_TEST_CASE(package_identity_missing_manifest_is_explicitly_unavailable);
    RUN_TEST_CASE(package_identity_rejects_oversized_and_nonregular_files_without_reading_them);
    pu_clear_setup();
}
