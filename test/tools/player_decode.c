#include "host/native_default.h"
#include "render/shader_parameters.h"
#include "render/shader_publish.h"
#include "runtime/component_catalog.h"
#include "runtime/native_prepared.h"
#include "runtime/player_prepared.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_native_catalog_enable();
static const char *artifact, *budget_text;
static native_capabilities *native;
static char *root;
static player *decoded;
static bool scene_ready;

/* Decode actual resources and executable modules, then stop before Begin,
 * resource publication or any window/GPU work. */
static bool stop_before_publication(void *, void *, const scene *, char *error, size_t size)
{
    scene_ready = true;
    snprintf(error, size, "CPU decoding completed before scene publication");
    return false;
}
static void cleanup()
{
    if (decoded) {
        player_free(decoded);
    }
    decoded = NULL;
    native_capabilities_free(native);
    native = NULL;
    free(root);
    root = NULL;
}
TEST_CASE(prepared_player_decodes_with_explicit_shared_budget)
{
    size_t budget = 128u * 1024u * 1024u;
    if (budget_text) {
        char *end;
        errno = 0;
        unsigned long long bytes = strtoull(budget_text, &end, 10);
        REQUIRE(
            !errno && budget_text[0] != '-' && end != budget_text && !*end &&
            bytes && bytes <= SIZE_MAX
        );
        budget = (size_t)bytes;
    }
    char error[512] = { 0 };
    native = host_native_default_catalog_new(
        NULL,
        0,
        &RENDER_SHADER_PARAMETER_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
    REQUIRE(native);
    native_prepared_context context = { .native = native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    root = xstrdup(artifact);
    char *slash = strrchr(root, '/');
    if (slash) {
        if (slash == root) {
            slash++;
        }
        *slash = 0;
    } else {
        free(root);
        root = xstrdup(".");
    }
    player_resource_host resources = render_prepared_resources(root);
    resources.scene = stop_before_publication;
    player_host host = { .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
                         .linker = &linker,
                         .resources = resources,
                         .decoded_budget = 1 };
    decoded = player_open(artifact, &host, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(!decoded && !scene_ready && error[0]);
    host.decoded_budget = budget;
    decoded = player_open(artifact, &host, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!scene_ready) {
        fprintf(stderr, "Prepared player within %zu-byte budget: %s\n", budget, error);
    }
    REQUIRE(!decoded && scene_ready);
    return true;
}
void test_player_decode(const char *path, const char *budget)
{
    artifact = path;
    budget_text = budget;
    scene_ready = false;
    test_native_catalog_enable();
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prepared_player_decodes_with_explicit_shared_budget);
    pu_clear_setup();
}
