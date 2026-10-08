#include "assets/code_catalog.h"

#include "assets/code_source_intelligence.h"
#include "assets/refactor.h"
#include "assets/reference_adapters.h"
#include "assets/system_document.h"
#include "authoring/behavior_loader.h"
#include "graph/compiler/compile.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void test_check(int ok, const char *message);

static void write_source(const char *root, const char *relative, const char *text)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, relative);
    FILE *file = fopen(path, "wb");
    assert(file && text);
    assert(fputs(text, file) >= 0 && !fclose(file));
}

static char *identified_library(
    const char *text,
    const code_source_context *context,
    const char *name,
    char **metadata
)
{
    code_source *source = code_source_parse_context(text, NULL, context);
    test_check(source->asset != NULL, "Cross-library rename fixture parses without authored call pins");
    char *persisted = NULL;
    if (source->asset) {
        graph *function = behavior_asset_find_graph_by_name(source->asset, name);
        assert(function);
        function->graph_id = 1000;
        source->asset->graph->id_counter = 1001;
        persisted = code_source_format_with_metadata(source->asset, metadata);
    }
    code_source_free(source);
    return persisted;
}

static void cross_library_rename()
{
    char *definition_metadata = NULL, *caller_metadata = NULL;
    char *definition = identified_library(
        "library Shared { pure function Seven() -> (Result: int) { result "
        "Result: int = 7; } }",
        NULL,
        "Seven",
        &definition_metadata
    );
    if (!definition) {
        return;
    }
    code_source *original = code_source_parse(definition);
    test_check(
        code_source_metadata_restore(original, definition_metadata),
        "Fixture source restores independently saved identity metadata"
    );
    const behavior_asset *one[] = { original->asset };
    code_catalog *catalog = code_catalog_from_assets(one, 1);
    char *caller = identified_library(
        "library Caller { pure function Forward() -> (Result: int) { value "
        "chosen = Shared.Seven(); result Result: int = chosen.Result; } }",
        code_catalog_context(catalog),
        "Forward",
        &caller_metadata
    );
    code_catalog_free(catalog);
    code_source_free(original);
    if (!caller) {
        free(definition);
        free(definition_metadata);
        return;
    }
    const char *texts[] = { definition, caller };
    const char *metadata[] = { definition_metadata, caller_metadata };
    catalog = code_catalog_from_texts(texts, 2);
    test_check(code_catalog_restore_metadata(catalog, texts, metadata, 2), "Cross-library rename catalog restores independently owned callable identities");
    reference_index *index = reference_index_new();
    int shared = reference_index_add_asset(index, REFERENCE_ASSET_LIBRARY, "Shared", NULL, "library/Shared");
    int calling = reference_index_add_asset(index, REFERENCE_ASSET_LIBRARY, "Caller", NULL, "library/Caller");
    assert(shared == 0 && calling == 1);
    char error[256];
    for (int i = 0; i < 2; i++) {
        code_source *loaded = code_source_parse_context(texts[i], NULL, code_catalog_context(catalog));
        bool restored = code_source_metadata_restore(loaded, metadata[i]);
        behavior_asset *asset = restored ? code_source_build_behavior(loaded) : NULL;
        code_source_free(loaded);
        test_check(asset != NULL, "In-memory declarations retain overlapping graph identities per library");
        if (asset) {
            reference_index_add_graphs(index, i, asset);
        }
        behavior_asset_free(asset);
    }
    code_catalog_free(catalog);
    refactor_project project = { .index = index, .texts = texts, .metadata = metadata };
    refactor_plan plan = { 0 };
    int status = refactor_rename(
        &project,
        (reference_symbol){ .kind = REFERENCE_SYMBOL_GRAPH, .asset = shared, .id = 1000 },
        "Eight",
        &plan,
        error,
        sizeof error
    );
    if (status) {
        fprintf(stderr, "cross-library rename: %s\n", error);
    }
    test_check(!status && acount(plan.edits) == 2, "Cross-library rename updates definition and caller despite overlapping graph identities");
    const char *changed[] = { definition, caller };
    const char *changed_metadata[] = { definition_metadata, caller_metadata };
    for (int i = 0; i < acount(plan.edits); i++) {
        changed[plan.edits[i].asset] = plan.edits[i].text;
        changed_metadata[plan.edits[i].asset] = plan.edits[i].metadata;
        test_check(
            code_source_metadata_matches(plan.edits[i].metadata, plan.edits[i].text),
            "Refactor edits retain metadata bound to their exact executable "
            "source"
        );
    }
    if (!status) {
        /* Declaration discovery must tolerate the old caller while publishing
         * the new signature, before the caller's source is rewritten. */
        const char *transient[] = { changed[shared], caller };
        catalog = code_catalog_from_texts(transient, 2);
        test_check(
            code_catalog_restore_metadata(catalog, &changed[shared], &changed_metadata[shared], 1),
            "Renamed declaration recovers its existing signature identities"
        );
        const code_source_context *context = code_catalog_context(catalog);
        const graph *renamed = code_source_context_graph(
            context,
            NULL,
            "Shared",
            "Eight",
            GRAPH_FUNCTION,
            NULL,
            error,
            sizeof error
        );
        test_check(renamed && renamed->graph_id == 1000, "Renamed project signature retains its graph identity before old caller source is updated");
        behavior_asset *asset = code_catalog_load_behavior(catalog, changed[calling], error, sizeof error);
        test_check(asset != NULL && strstr(changed[calling], "Eight"), "Rewritten caller parses against the renamed declaration without legacy pin signatures");
        behavior_asset_free(asset);
        code_catalog_free(catalog);
    }
    refactor_plan_free(&plan);
    reference_index_free(index);
    free(caller);
    free(definition);
    free(caller_metadata);
    free(definition_metadata);
}

static bool reference_error(const char *text, const code_source_context *context, const char *message)
{
    code_source *source = code_source_parse_context(text, NULL, context);
    bool diagnosed = false;
    for (int i = 0; i < acount(source->diagnostics); i++) {
        diagnosed |= strstr(source->diagnostics[i].message, message) != NULL;
    }
    bool rejected = !source->asset && diagnosed;
    code_source_free(source);
    return rejected;
}

static void prototype_inventory()
{
    char root[] = "/tmp/chibi-code-symbols-XXXXXX", path[512];
    assert(mkdtemp(root));
    snprintf(path, sizeof path, "%s/prototypes", root);
    assert(!mkdir(path, 0700));
    write_source(root, "prototypes/Emitter.kdl", "name \"MovementEmitter\"\ncomponents {}\n");
    code_catalog *catalog = code_catalog_load(root);
    char error[256];
    behavior_asset *asset = code_catalog_load_behavior(
        catalog,
        "behavior Effects { on Begin { await spawn(MovementEmitter); } }",
        error,
        sizeof error
    );
    test_check(asset && !code_catalog_error_count(catalog), "Project prototype definitions supply quote-free source references");
    behavior_asset_free(asset);
    code_catalog_free(catalog);
    snprintf(path, sizeof path, "%s/prototypes/Emitter.kdl", root);
    assert(!unlink(path));
    snprintf(path, sizeof path, "%s/prototypes", root);
    assert(!rmdir(path));
    assert(!rmdir(root));
}

static void snapshot_library_metadata()
{
    code_source *source = code_source_parse("library Shared { pure function Seven() -> (Result: int) { result Result: int = 7; } }");
    assert(source->asset);
    graph *function = behavior_asset_find_graph_by_name(source->asset, "Seven");
    assert(function);
    function->graph_id = 1000;
    source->asset->graph->id_counter = 1001;
    char *metadata = NULL;
    char *text = code_source_format_with_metadata(source->asset, &metadata);
    const char *texts[] = { text }, *presentations[] = { metadata };
    code_catalog *catalog = code_catalog_from_texts(texts, 1);
    bool restored = text && metadata &&
                    code_catalog_restore_metadata(catalog, texts, presentations, 1);
    const code_source_context *context = code_catalog_context(catalog);
    const graph *signature = restored && context->library_count == 1
                               ? behavior_asset_find_graph_by_name(context->libraries[0], "Seven")
                               : NULL;
    test_check(signature && signature->graph_id == 1000, "Snapshot library catalogs retain restored callable identities before loading consumers");
    char error[256];
    behavior_asset *consumer = restored
                                 ? code_catalog_load_behavior(catalog, "behavior Consumer { value chosen = Shared.Seven(); }", error, sizeof error)
                                 : NULL;
    bool resolved = false;
    for (int i = 0; consumer && i < acount(consumer->graph->nodes); i++) {
        resolved |= consumer->graph->nodes[i]->reference_id == 1000;
    }
    test_check(consumer && resolved, "Consumers bind to restored snapshot library signatures");
    behavior_asset_free(consumer);
    code_catalog_free(catalog);
    free(text);
    free(metadata);
    code_source_free(source);
}

static void snapshot_references()
{
    code_catalog *catalog = code_catalog_from_texts(NULL, 0);
    char name[] = "MovementEmitter";
    code_catalog_add_reference(catalog, CODE_REFERENCE_PROTOTYPE, name);
    name[0] = 'X';
    char error[256];
    behavior_asset *asset = code_catalog_load_behavior(
        catalog,
        "behavior Effects { on Begin { await spawn(MovementEmitter); } }",
        error,
        sizeof error
    );
    test_check(asset != NULL, "Snapshot catalogs own seeded prototype identities without consulting disk");
    behavior_asset_free(asset);
    code_catalog_free(catalog);
}

static void typed_references()
{
    code_reference names[] = { { CODE_REFERENCE_PROTOTYPE, "MovementEmitter" },
                               { CODE_REFERENCE_PROTOTYPE, "MovementEmitter" } };
    code_source_context context = { .references = names, .reference_count = 1 };
    code_source *source = code_source_parse_context(
        "behavior Effects { on Begin { await spawn(MovementEmitter); } }",
        NULL,
        &context
    );
    test_check(source->asset != NULL, "Quote-free prototype reference resolves in its typed native argument");
    char *printed = source->asset ? code_source_format(source->asset) : NULL;
    test_check(
        printed && strstr(printed, "Prototype: MovementEmitter") &&
            !strstr(printed, "\"MovementEmitter\""),
        "Prototype printing preserves its semantic symbol spelling"
    );
    free(printed);
    code_source_free(source);
    test_check(
        reference_error("behavior Effects { on Begin { await spawn(Missing); } }", &context, "Unknown prototype"),
        "Missing prototype symbols produce typed diagnostics"
    );
    context.reference_count = 2;
    test_check(
        reference_error("behavior Effects { on Begin { await spawn(MovementEmitter); } }", &context, "Ambiguous prototype"),
        "Duplicate catalog symbols cannot silently select a prototype"
    );
    context.reference_count = 1;
    source = code_source_parse_context(
        "behavior ComponentRef { value read = get_component(Component: "
        "GameVelocity, Entity: self); }",
        NULL,
        &context
    );
    test_check(source->asset != NULL, "Component symbols resolve through reflected component identity");
    code_source_free(source);
    test_check(
        reference_error("behavior Effects { on Begin { await spawn(GameVelocity); } }", &context, "Wrong kind"),
        "A component cannot resolve as a prototype"
    );
}

void run_code_catalog_checks()
{
    snapshot_library_metadata();
    snapshot_references();
    prototype_inventory();
    typed_references();
    cross_library_rename();
    const char
        *alpha = "library Alpha { pure function Forward() -> (Result: int) { "
                 "value chosen = Zulu.Seven(); result Result: int = "
                 "chosen.Result; } }";
    const char
        *zulu = "library Zulu { pure function Seven() -> (Result: int) { "
                "result Result: int = 7; } }";
    const char
        *consumer = "behavior Consumer { value chosen = Alpha.Forward(); }";
    const char
        *system = "system ConsumerSystem { value chosen = Alpha.Forward(); }";
    char root[] = "/tmp/chibi-code-catalog-XXXXXX";
    assert(mkdtemp(root));
    const char *folders[] = { "libraries", "behaviors", "systems" };
    char path[512];
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", root, folders[i]);
        assert(!mkdir(path, 0700));
    }
    write_source(root, "libraries/Alpha.chibi", alpha);
    code_source *authored = code_source_parse(zulu);
    test_check(authored->asset != NULL, "Library fixture parses before assigning authored graph identity");
    char *persisted = NULL;
    if (authored->asset) {
        graph *function = behavior_asset_find_graph_by_name(authored->asset, "Seven");
        assert(function);
        function->graph_id = 1000;
        authored->asset->graph->id_counter = 1001;
        persisted = code_source_persist(authored);
    }
    write_source(root, "libraries/Zulu.chibi", persisted ? persisted : zulu);
    free(persisted);
    code_source_free(authored);
    write_source(root, "behaviors/Consumer.chibi", consumer);
    write_source(root, "systems/ConsumerSystem.chibi", system);
    code_catalog *catalog = code_catalog_load(root);
    test_check(!code_catalog_error_count(catalog), "Project library signature pass resolves forward filename references without authored call pins");
    const code_source_context *context = code_catalog_context(catalog);
    test_check(context->library_count == 2, "Project catalog retains both fully lowered libraries");
    char error[256];
    behavior_asset *asset = code_catalog_load_behavior(catalog, consumer, error, sizeof error);
    test_check(asset != NULL, "Behavior source resolves project library signatures before lowering");
    if (!asset) {
        fprintf(stderr, "catalog consumer: %s\n", error);
    } else {
        behavior_asset *linked = behavior_asset_link_libraries(
            asset,
            context->libraries,
            context->library_count,
            error,
            sizeof error
        );
        test_check(linked != NULL, "Forward project calls link against complete library bodies with authored graph identities");
        behavior_asset_free(linked);
    }
    behavior_asset_free(asset);
    code_catalog_free(catalog);

    const char *memory[] = { alpha, zulu };
    catalog = code_catalog_from_texts(memory, 2);
    asset = code_catalog_load_behavior(catalog, consumer, error, sizeof error);
    test_check(asset != NULL, "Unsaved project source declarations resolve the same callable signatures");
    behavior_asset_free(asset);
    code_catalog_free(catalog);

    ecs_world *ecs = ecs_world_new();
    game_preview_world *world = game_preview_world_new(ecs);
    test_check(app_behavior_fixtures_load(world, NULL, root) == 4, "Runtime project load registers libraries before dependent behaviors and systems");
    test_check(
        game_preview_find_definition(world, "Consumer") &&
            game_preview_find_system(world, "ConsumerSystem"),
        "Runtime retains dependent behavior and system definitions"
    );
    game_system_document *document = game_preview_load_system(world, system, error, sizeof error);
    test_check(document != NULL, "Runtime system reload parses and links against applied project libraries");
    compile_report report;
    compile_report_init(&report);
    bool executable = document && !behavior_asset_compile_for_system(
                                      game_system_document_behavior_asset(document),
                                      &report
                                  );
    test_check(executable, "Reloaded systems contain executable linked library bodies, not unresolved external calls");
    compile_report_free(&report);
    game_system_document_free(document);
    game_preview_world_free(world);
    ecs_world_free(ecs);
    const char *files[] = { "libraries/Alpha.chibi",
                            "libraries/Zulu.chibi",
                            "behaviors/Consumer.chibi",
                            "systems/ConsumerSystem.chibi" };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i]);
        assert(!unlink(path));
    }
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", root, folders[i]);
        assert(!rmdir(path));
    }
    assert(!rmdir(root));
}
