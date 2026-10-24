#include "authoring/asset_compiler_host.h"

#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "execution/function.h"
#include "support/array.h"
#include "support/asset_value.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

/* Public installed-component consumer; the authored generator owns the resource. */
static av_doc *document;
static scene_document *scene;
static prototype *object;
static const prototype_registry registry;
static void cleanup()
{
    scene_document_free(scene);
    scene = NULL;
    prototype_free(object);
    object = NULL;
    av_doc_free(document);
    document = NULL;
    asset_source_release_thread();
}
static asset_compiler_options options()
{
    asset_compiler_options value = *asset_compiler_packaged_options();
    value.cache_directory = "";
    return value;
}
static av_doc *read_document(const char *source)
{
    asset_compiler_options config = options();
    asset_compiler_diagnostic error;
    av_doc *value = asset_source_read(source, "/project/asset/Native.chibi", &config, &error);
    if (!value) {
        fprintf(
            stderr,
            "native DTO Build: %s:%d:%d %s\n",
            error.source,
            error.line,
            error.column,
            error.message
        );
    }
    return value;
}
static void drop_source()
{
    av_doc_free(document);
    document = NULL;
    asset_source_release_thread();
}
TEST_CASE(asset_source_public_scene_build_and_reuse)
{
    static const char
        SOURCE[] = "package probe\nimport data \"game:assets\"\n"
                   "pub Build :: () -> byte[] { return "
                   "data.Encode(data.Scene(\"Empty\", [], [])); }\n";
    asset_compiler_options config = options();
    asset_compiler_diagnostic error;
    for (int i = 0; i < 2; i++) {
        document = asset_source_read(SOURCE, "/project/asset/Probe.chibi", &config, &error);
        if (!document) {
            fprintf(
                stderr,
                "asset Build: %s:%d:%d %s\n",
                error.source,
                error.line,
                error.column,
                error.message
            );
        }
        REQUIRE(document && !error.message[0]);
        av_value *root = av_doc_root(document);
        REQUIRE(av_is_obj(root));
        const av_value *name = av_obj_get(root, "name");
        REQUIRE(av_is_str(name) && !strcmp(av_get_str(name), "Empty"));
        REQUIRE(av_get_uint(av_obj_get(root, "version")) == 1);
        const av_value *entities = av_obj_get(root, "entities");
        REQUIRE(av_is_arr(entities) && !av_arr_size(entities));
        asset_compiler_cache_stats stats = asset_source_cache_stats();
        REQUIRE(stats.compile_attempts == 1 && stats.preparation_attempts == 1);
        REQUIRE(stats.evaluations == (uint64_t)i + 1 && stats.memory_hits == (uint64_t)i);
        REQUIRE(!behavior_execution_scope_active());
        av_doc_free(document);
        document = NULL;
    }
    return true;
}
TEST_CASE(asset_source_authored_validation_rejects_duplicate_field)
{
    static const char SOURCE[] = "package probe\nimport data \"game:assets\"\n"
                                 "pub Build :: () -> byte[] { return "
                                 "data.Encode(data.Scene(\"Empty\", [], "
                                 "[data.Field.Named(\"name\", "
                                 "data.Value.Text(\"Other\"))])); }\n";
    asset_compiler_options config = options();
    asset_compiler_diagnostic error;
    document = asset_source_read(SOURCE, "/project/asset/Probe.chibi", &config, &error);
    fprintf(
        stderr,
        "authored rejection: %s:%d:%d %s\n",
        error.source,
        error.line,
        error.column,
        error.message
    );
    REQUIRE(!document && strstr(error.message, "Duplicate asset object field"));
    REQUIRE(asset_source_cache_stats().evaluations == 1 && !behavior_execution_scope_active());
    return true;
}
TEST_CASE(asset_source_malformed_build_reports_position)
{
    static const char SOURCE[] = "package probe\npub Build :: () -> byte[] { "
                                 "return Missing(); }\n";
    asset_compiler_options config = options();
    asset_compiler_diagnostic error;
    document = asset_source_read(SOURCE, "/project/asset/Probe.chibi", &config, &error);
    fprintf(
        stderr,
        "compile rejection: %s:%d:%d %s\n",
        error.source,
        error.line,
        error.column,
        error.message
    );
    REQUIRE(!document && strstr(error.message, "Unknown function 'Missing'"));
    REQUIRE(
        error.document == 0 && error.line == 2 && error.column > 0 &&
        error.end > error.start
    );
    REQUIRE(!asset_source_cache_stats().evaluations && !behavior_execution_scope_active());
    return true;
}
TEST_CASE(asset_source_native_scene_values_own_their_storage)
{
    static const char *const SOURCES[] = {
        "package probe\nimport data \"game:assets\"\n"
        "pub Build :: () -> byte[] { return data.Encode(data.Scene(\"Empty\", "
        "[], [])); }\n",
        "package probe\nimport data \"game:assets\"\n"
        "pub Build :: () -> byte[] { return data.Encode(data.Scene(\"Owned\", "
        "[data.EntityDefinition.Named(\"Root\", \"Base\", "
        "[data.Field.Named(\"Unregistered\", data.Object("
        "[data.Field.Named(\"label\", data.Value.Text(\"kept\"))]))], [])], "
        "[])); }\n"
    };
    for (int i = 0; i < 2; i++) {
        document = read_document(SOURCES[i]);
        REQUIRE(document);
        const av_value *version = av_obj_get(av_doc_root(document), "version");
        REQUIRE(av_is_uint(version) && av_get_uint(version) == 1);
        char error[256] = { 0 };
        scene = scene_document_load_value(av_doc_root(document), error, sizeof error);
        REQUIRE(scene && !error[0]);
        drop_source();
        REQUIRE(!behavior_execution_scope_active());
        REQUIRE(!strcmp(scene->name, i ? "Owned" : "Empty"));
        REQUIRE(acount(scene->entities) == i);
        if (i) {
            REQUIRE(scene_document_entity_index(scene, "Root") == 0);
            REQUIRE(scene->entities[0].parent == -1);
            REQUIRE(!strcmp(scene->entities[0].prototype, "Base"));
            const av_value *fields = av_obj_get(scene->entities[0].components, "Unregistered");
            const av_value *label = av_obj_get(fields, "label");
            REQUIRE(av_is_str(label) && !strcmp(av_get_str(label), "kept"));
        }
        scene_document_free(scene);
        scene = NULL;
    }
    return true;
}
TEST_CASE(asset_source_native_prototype_owns_name_and_identity)
{
    static const char
        SOURCE[] = "package probe\nimport data \"game:assets\"\n"
                   "pub Build :: () -> byte[] { return data.Encode("
                   "data.Prototype(\"Seed\", [], [], [], "
                   "[data.Field.Named(\"identity\", "
                   "data.Value.Text(\"owned-identity\"))])); }\n";
    document = read_document(SOURCE);
    REQUIRE(document);
    char error[256] = { 0 };
    object = prototype_load_value(av_doc_root(document), &registry, error, sizeof error);
    REQUIRE(object && !error[0]);
    drop_source();
    REQUIRE(!behavior_execution_scope_active());
    REQUIRE(!strcmp(object->name, "Seed"));
    REQUIRE(!strcmp(object->identity, "owned-identity"));
    REQUIRE(
        !acount(object->components) && !acount(object->includes) &&
        !acount(object->graphs)
    );
    REQUIRE(!object->registry->component_count);
    return true;
}
TEST_CASE(asset_source_native_readers_keep_kind_shape_and_catalog_checks)
{
    static const char *const SOURCES[] = {
        "package probe\nimport data \"game:assets\"\n"
        "pub Build :: () -> byte[] { return data.Encode(data.Object("
        "[data.Field.Named(\"version\", data.Value.Int(1)), "
        "data.Field.Named(\"name\", data.Value.Text(\"Signed\")), "
        "data.Field.Named(\"entities\", data.List([]))])); }\n",
        "package probe\nimport data \"game:assets\"\n"
        "pub Build :: () -> byte[] { return data.Encode(data.Object("
        "[data.Field.Named(\"version\", data.Value.UInt(1)), "
        "data.Field.Named(\"name\", data.Value.Text(\"WrongShape\")), "
        "data.Field.Named(\"entities\", data.Value.Text(\"not-an-array\"))])); "
        "}\n"
    };
    for (int i = 0; i < 2; i++) {
        document = read_document(SOURCES[i]);
        REQUIRE(document);
        if (!i) {
            const av_value *version = av_obj_get(av_doc_root(document), "version");
            REQUIRE(av_is_sint(version) && av_get_sint(version) == 1);
        }
        char error[256] = { 0 };
        scene = scene_document_load_value(av_doc_root(document), error, sizeof error);
        REQUIRE(!scene);
        REQUIRE(strstr(error, "scene document requires version 1, name, and entities"));
        drop_source();
    }
    static const char
        UNKNOWN[] = "package probe\nimport data \"game:assets\"\n"
                    "pub Build :: () -> byte[] { return data.Encode("
                    "data.Prototype(\"Unknown\", "
                    "[data.Field.Named(\"Unregistered\", data.Object([]))], "
                    "[], [], [])); }\n";
    document = read_document(UNKNOWN);
    REQUIRE(document);
    char error[256] = { 0 };
    object = prototype_load_value(av_doc_root(document), &registry, error, sizeof error);
    REQUIRE(!object);
    REQUIRE(strstr(error, "component Unregistered is unavailable"));
    REQUIRE(!behavior_execution_scope_active());
    return true;
}
void test_language_asset_compiler_host()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(asset_source_public_scene_build_and_reuse);
    RUN_TEST_CASE(asset_source_authored_validation_rejects_duplicate_field);
    RUN_TEST_CASE(asset_source_malformed_build_reports_position);
    RUN_TEST_CASE(asset_source_native_scene_values_own_their_storage);
    RUN_TEST_CASE(asset_source_native_prototype_owns_name_and_identity);
    RUN_TEST_CASE(asset_source_native_readers_keep_kind_shape_and_catalog_checks);
    pu_clear_setup();
}
