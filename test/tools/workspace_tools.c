#include "authoring/ui_source_provider.h"
#include "authoring/workspace_css.h"
#include "authoring/workspace_documents.h"
#include "authoring/workspace_saved.h"
#include "support/sha256.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif
TEST_CASE(saved_rows_copy_exact_values_and_stale_edits_do_not_mutate)
{
    save *store = save_new();
    arena a = { 0 };
    char error[256];
    REQUIRE(save_set(store, "score", "001"));
    REQUIRE(save_set(store, "name", "A<&B"));
    workspace_saved_row *rows;
    int total, returned;
    uint64_t revision;
    REQUIRE(
        workspace_saved_query(store, 0, 256, &a, &rows, &total, &returned, &revision, error, sizeof error)
    );
    REQUIRE(
        total == 2 && returned == 2 && !strcmp(rows[0].key, "score") &&
        !strcmp(rows[0].value, "001")
    );
    REQUIRE(workspace_saved_write(store, revision, "name", "Exact text", error, sizeof error));
    REQUIRE(!strcmp(rows[1].value, "A<&B"));
    REQUIRE(!workspace_saved_write(store, revision, "score", "7", error, sizeof error));
    REQUIRE(!strcmp(save_get(store, "score"), "001"));
    REQUIRE(
        !workspace_saved_write(store, save_revision(store), "missing", "7", error, sizeof error)
    );
    REQUIRE(save_count(store) == 2);
    REQUIRE(
        !workspace_saved_query(store, 0, 257, &a, &rows, &total, &returned, &revision, error, sizeof error)
    );
    REQUIRE(
        workspace_saved_query(store, 1, 1, &a, &rows, &total, &returned, &revision, error, sizeof error)
    );
    REQUIRE(returned == 1 && !strcmp(rows[0].value, "Exact text"));
    save_free(store);
    REQUIRE(!strcmp(rows[0].key, "name"));
    arena_free(&a);
    return true;
}
#if !defined(_WIN32)
TEST_CASE(css_inventory_preserves_import_order_and_edits_exact_source)
{
    char root[1024], path[1200];
    snprintf(root, sizeof root, "%s/css-tools-XXXXXX", tool_root());
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/ui", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/ui/Page.rml", root);
    tool_write(path, "<body><use ui=\"Badge\"/></body>");
    snprintf(path, sizeof path, "%s/content/ui/Page.rcss", root);
    tool_write(path, "@import \"base.rcss\";\nbody { --size: 12dp; }\n");
    snprintf(path, sizeof path, "%s/content/ui/base.rcss", root);
    tool_write(path, "/* preserve */\n:root { --ink: #abc; }\n");
    snprintf(path, sizeof path, "%s/content/ui/Badge.rml", root);
    tool_write(path, "<span/>");
    snprintf(path, sizeof path, "%s/content/ui/Badge.rcss", root);
    tool_write(path, ".badge { --weight: 400; }\n");
    workspace_document_status status;
    workspace_documents *documents = workspace_documents_new(root, &status);
    REQUIRE(documents && !status);
    char error[256] = "";
    ui_source_provider *provider = ui_source_provider_new(root, "content", documents, error, sizeof error);
    REQUIRE(provider);
    arena a = { 0 };
    workspace_css_row *rows;
    int count;
    REQUIRE(workspace_css_query(provider, "Page", &a, &rows, &count, error, sizeof error));
    REQUIRE(
        count == 3 && !strcmp(rows[0].name, "--ink") &&
        !strcmp(rows[1].name, "--size") && !strcmp(rows[2].name, "--weight")
    );
    REQUIRE(workspace_document_handles(documents, NULL, 0) == 0);
    snprintf(path, sizeof path, "%s/project.kdl", root);
    tool_write(path, "version 25\nname \"CSS Fixture\"\ncontent \"content\"\nstart_scene \"Scene\"\nui \"Page\"\n");
    snprintf(path, sizeof path, "%s/Scene.kdl", root);
    tool_write(path, "name \"Scene\"\nui \"\"\n");
    snprintf(path, sizeof path, "%s/content/ui/Other.rml", root);
    tool_write(path, "<body/>");
    snprintf(path, sizeof path, "%s/content/ui/Other.rcss", root);
    tool_write(path, "body { --explicit: 1; }");
    int scene;
    workspace_document_view scene_view;
    REQUIRE(!workspace_document_open(documents, "Scene.kdl", &scene));
    REQUIRE(!workspace_document_read(documents, scene, &scene_view));
    workspace_css_service service = { .documents = documents, .provider = provider };
    workspace_css_row *selected_rows;
    int selected_count;
    REQUIRE(workspace_css_document_query(
        &service,
        scene,
        scene_view.revision,
        &a,
        &selected_rows,
        &selected_count,
        error,
        sizeof error
    ));
    REQUIRE(selected_count == 3 && !strcmp(selected_rows[0].name, "--ink"));
    int old_revision = scene_view.revision;
    REQUIRE(!workspace_document_edit(
        documents,
        scene,
        old_revision,
        0,
        (int)scene_view.bytes,
        "name \"Scene\"\nui \"Other\"\n"
    ));
    REQUIRE(
        !workspace_css_document_query(&service, scene, old_revision, &a, &selected_rows, &selected_count, error, sizeof error)
    );
    REQUIRE(!workspace_document_read(documents, scene, &scene_view));
    REQUIRE(workspace_css_document_query(
        &service,
        scene,
        scene_view.revision,
        &a,
        &selected_rows,
        &selected_count,
        error,
        sizeof error
    ));
    REQUIRE(selected_count == 1 && !strcmp(selected_rows[0].name, "--explicit"));

    int document, revision;
    REQUIRE(workspace_css_edit(
        documents,
        rows[0].path,
        rows[0].fingerprint,
        rows[0].start,
        rows[0].end,
        rows[0].value,
        "#aabbcc",
        &document,
        &revision,
        error,
        sizeof error
    ));
    snprintf(path, sizeof path, "%s/content/ui/base.rcss", root);
    char *written = tool_read(path);
    REQUIRE(!strcmp(written, "/* preserve */\n:root { --ink: #aabbcc; }\n"));
    free(written);
    workspace_document_view before, after;
    REQUIRE(!workspace_document_read(documents, document, &before));
    REQUIRE(!workspace_css_edit(
        documents,
        rows[0].path,
        rows[0].fingerprint,
        rows[0].start,
        rows[0].end,
        rows[0].value,
        "black",
        &document,
        &revision,
        error,
        sizeof error
    ));
    REQUIRE(
        !workspace_document_read(documents, before.document, &after) &&
        before.revision == after.revision
    );
    REQUIRE(!strcmp(rows[0].value, "#abc"));
    REQUIRE(workspace_css_query(provider, "Page", &a, &rows, &count, error, sizeof error));
    REQUIRE(!strcmp(rows[0].value, "#aabbcc"));
    int page;
    REQUIRE(!workspace_document_open(documents, "content/ui/Page.rcss", &page));
    REQUIRE(!workspace_document_read(documents, page, &after));
    REQUIRE(
        !workspace_document_edit(documents, page, after.revision, 0, (int)after.bytes, "body { --draft: 9dp; }")
    );
    REQUIRE(workspace_css_query(provider, "Page", &a, &rows, &count, error, sizeof error));
    REQUIRE(count == 2 && !strcmp(rows[0].name, "--draft") && !strcmp(rows[1].name, "--weight"));
    REQUIRE(
        !workspace_css_query(provider, "../Page", &a, &rows, &count, error, sizeof error) &&
        !count
    );
    arena_free(&a);
    ui_source_provider_free(provider);
    workspace_documents_free(documents);
    tool_remove_tree(root);
    return true;
}
#endif
void test_tool_workspace_tools()
{
    RUN_TEST_CASE(saved_rows_copy_exact_values_and_stale_edits_do_not_mutate);
#if !defined(_WIN32)
    RUN_TEST_CASE(css_inventory_preserves_import_order_and_edits_exact_source);
#endif
}
