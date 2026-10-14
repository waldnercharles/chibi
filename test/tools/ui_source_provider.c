#include "authoring/ui_source_provider.h"

#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>
static workspace_documents *documents;
static ui_source_provider *provider;
static void setup()
{
    workspace_document_status status;
    documents = workspace_documents_new("test/fixtures/ui-source-preview", &status);
    char error[256];
    provider = ui_source_provider_new("test/fixtures/ui-source-preview", "content", documents, error, sizeof error);
}
static void teardown()
{
    ui_source_provider_free(provider);
    workspace_documents_free(documents);
}
TEST_CASE(ui_provider_snapshots_unsaved_widget_sources_and_preserves_page_attributes)
{
    REQUIRE(documents && provider);
    render_ui_loader loader = ui_source_provider_loader(provider);
    void *state = loader.open_context(loader.context, "", "Main", true);
    char error[256];
    ui_document document = { 0 };
    REQUIRE(loader.read(state, 0, &document, error, sizeof error) == 1);
    REQUIRE(document.body && strstr(document.body, "preview"));
    REQUIRE(
        strstr(document.body, "data-source-start=\"0\"") &&
        strstr(document.body, "data-src=\"Main:1\"")
    );
    REQUIRE(strstr(document.rml, "Saved badge") && strstr(document.rml, "data-src"));
    REQUIRE(strstr(document.rcss, "40a0ff"));
    char *saved = xstrdup(document.rml);
    ui_document_free(&document);
    REQUIRE(loader.read(state, 1, &document, error, sizeof error) == 0);
    int handle;
    REQUIRE(!workspace_document_open(documents, "content/ui/Badge.rml", &handle));
    REQUIRE(ui_source_provider_current(provider));
    workspace_document_view view;
    REQUIRE(!workspace_document_read(documents, handle, &view));
    REQUIRE(
        !workspace_document_edit(documents, handle, view.revision, 0, (int)view.bytes, "<span>Unsaved badge</span>")
    );
    REQUIRE(loader.read(state, 2, &document, error, sizeof error) == 0);
    REQUIRE(!ui_source_provider_current(provider));
    REQUIRE(ui_source_provider_refresh(provider, error, sizeof error));
    REQUIRE(loader.read(state, 3, &document, error, sizeof error) == 1);
    REQUIRE(ui_source_provider_current(provider));
    REQUIRE(strstr(document.rml, "Unsaved badge") && strstr(saved, "Saved badge"));
    free(saved);
    ui_document_free(&document);
    loader.close(state);
    return true;
}
TEST_CASE(ui_provider_refuses_unsafe_names_without_creating_documents)
{
    REQUIRE(provider);
    render_ui_loader loader = ui_source_provider_loader(provider);
    void *state = loader.open_context(loader.context, "", "../Main", true);
    char error[256];
    ui_document document = { 0 };
    REQUIRE(loader.read(state, 0, &document, error, sizeof error) == -1);
    REQUIRE(strstr(error, "invalid") && !document.rml);
    REQUIRE(workspace_document_handles(documents, NULL, 0) == 0);
    loader.close(state);
    return true;
}
TEST_CASE(ui_provider_bounds_repeated_widget_expansion_work)
{
    REQUIRE(provider);
    int main, badge;
    REQUIRE(!workspace_document_open(documents, "content/ui/Main.rml", &main));
    REQUIRE(!workspace_document_open(documents, "content/ui/Badge.rml", &badge));
    workspace_document_view view;
    REQUIRE(!workspace_document_read(documents, badge, &view));
    size_t size = 512u * 1024u;
    char *large = xmalloc(size + 1);
    memset(large, 'x', size);
    large[size] = 0;
    REQUIRE(
        !workspace_document_edit(documents, badge, view.revision, 0, (int)view.bytes, large)
    );
    free(large);
    char markup[2048] = "";
    for (int i = 0; i < 64; i++) {
        strcat(markup, "<use ui='Badge'/>");
    }
    REQUIRE(!workspace_document_read(documents, main, &view));
    REQUIRE(
        !workspace_document_edit(documents, main, view.revision, 0, (int)view.bytes, markup)
    );
    char error[256];
    REQUIRE(ui_source_provider_refresh(provider, error, sizeof error));
    render_ui_loader loader = ui_source_provider_loader(provider);
    void *state = loader.open_context(loader.context, "", "Main", true);
    ui_document document = { 0 };
    REQUIRE(loader.read(state, 0, &document, error, sizeof error) == -1);
    REQUIRE(strstr(error, "16 MiB") && !document.rml);
    loader.close(state);
    return true;
}
void test_tool_ui_source_provider()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(ui_provider_snapshots_unsaved_widget_sources_and_preserves_page_attributes);
    RUN_TEST_CASE(ui_provider_refuses_unsafe_names_without_creating_documents);
    RUN_TEST_CASE(ui_provider_bounds_repeated_widget_expansion_work);
    pu_clear_setup();
}
