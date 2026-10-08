#include "assets/code_source.h"
#include "automation/controller.h"
#include "suite_internal.h"
#include "support/text_file.h"
#include "third_party/yyjson/yyjson.h"

#include <sys/stat.h>

static yyjson_doc *analyze_project(const char *project, const char *source, int offset, int *status)
{
    yyjson_mut_doc *request = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(request);
    yyjson_mut_doc_set_root(request, root);
    yyjson_mut_obj_add_int(request, root, "contract", CHIBI_AUTOMATION_CONTRACT);
    yyjson_mut_obj_add_str(request, root, "operation", "analyze_code");
    yyjson_mut_obj_add_strcpy(request, root, "source", source);
    if (offset >= 0) {
        yyjson_mut_obj_add_int(request, root, "offset", offset);
    }
    char *json = yyjson_mut_write(request, 0, NULL);
    char *response = chibi_automation_handle(json, project, status);
    yyjson_doc *document = yyjson_read(response, strlen(response), 0);
    free(response);
    free(json);
    yyjson_mut_doc_free(request);
    return document;
}

static yyjson_doc *analyze(const char *source, int offset, int *status)
{
    return analyze_project("/missing-project-root", source, offset, status);
}

static void write_source(const char *path, const char *source)
{
    FILE *file = fopen(path, "wb");
    assert(file && fputs(source, file) >= 0 && !fclose(file));
}

static void persisted_source(const char *source)
{
    char project[] = "/tmp/chibi-code-XXXXXX";
    test_check(mkdtemp(project) != NULL, "create code authoring project");
    char path[256];
    snprintf(path, sizeof path, "%s/content", project);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/content/behaviors", project);
    mkdir(path, 0700);
    int status;
    snprintf(path, sizeof path, "%s/content/behaviors/Unsaved.chibi", project);
    write_source(path, source);
    code_source *authored_source = code_source_parse(source);
    test_check(authored_source && authored_source->asset, "authored source builds before file persistence");
    test_check(code_source_metadata_save(authored_source, path), "authored source stores optional metadata beside executable text");
    char *saved = text_file_read(path);
    if (strstr(source, "// Keep this authored comment.")) {
        test_check(
            saved && strstr(saved, "// Keep this authored comment.") &&
                strstr(saved, "state int count = 0;") && strstr(saved, "on Tick() {}"),
            "file authoring preserves statements and comments"
        );
        test_check(
            saved && !strstr(saved, "@node") && !strstr(saved, "@authoring"),
            "authored executable source has no machine annotations"
        );
    }
    behavior_asset *authored = code_source_build_behavior(authored_source);
    code_source *reopened_source = saved ? code_source_parse(saved) : NULL;
    test_check(
        reopened_source && code_source_metadata_load(reopened_source, path),
        "reopened source restores optional metadata beside executable text"
    );
    behavior_asset *reopened = reopened_source
                                 ? code_source_build_behavior(reopened_source)
                                 : NULL;
    test_check(
        authored && reopened && behavior_asset_equal(authored, reopened),
        "persisted structured source reopens as the same native graph"
    );
    behavior_asset_free(authored);
    behavior_asset_free(reopened);
    code_source_free(reopened_source);
    code_source_free(authored_source);
    test_check(chibi_automation_format_assets(project, true) == 0, "canonical check keeps structured behavior source");
    test_check(chibi_automation_format_assets(project, false) == 0, "format preserves structured source and comments");
    char *formatted = text_file_read(path);
    test_check(formatted && saved && !strcmp(formatted, saved), "format is idempotent and retains structured source comments");
    free(formatted);
    char *reply = chibi_automation_handle(
        "{\"contract\":1,\"operation\":\"read_asset\",\"kind\":"
        "\"behavior\",\"name\":\"Unsaved\",\"complete\":{\"line\":1,"
        "\"column\":1}}",
        project,
        &status
    );
    yyjson_doc *read = yyjson_read(reply, strlen(reply), 0);
    test_check(
        status == CHIBI_AUTOMATION_OK &&
            yyjson_is_obj(
                yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(read), "result"), "document")
            ),
        "readAsset exposes the structured behavior native graph"
    );
    if (strstr(source, "behavior DeclaredName")) {
        const char *name = yyjson_get_str(
            yyjson_ptr_get(yyjson_doc_get_root(read), "/result/document/name")
        );
        test_check(name && !strcmp(name, "DeclaredName"), "readAsset retains the declaration name when the filename differs");
    }
    yyjson_doc_free(read);
    free(reply);
    char metadata_path[300];
    snprintf(metadata_path, sizeof metadata_path, "%s.meta", path);
    test_check(unlink(metadata_path) == 0, "remove optional presentation metadata");
    reply = chibi_automation_handle(
        "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":"
        "\"behavior\",\"name\":\"Unsaved\"}",
        project,
        &status
    );
    test_check(status == CHIBI_AUTOMATION_OK, "validateAsset compiles structured behavior source without optional metadata");
    free(reply);
    yyjson_doc *response = analyze_project(project, "behavior {", -1, &status);
    test_check(
        status == CHIBI_AUTOMATION_OK &&
            !yyjson_get_bool(yyjson_ptr_get(yyjson_doc_get_root(response), "/result/valid")),
        "invalid unsaved replacement reports a failed candidate"
    );
    yyjson_doc_free(response);
    char *retained = text_file_read(path);
    test_check(retained && saved && !strcmp(saved, retained), "analysis of invalid source retains the authored file");
    free(saved);
    free(retained);
    unlink(path);
    snprintf(path, sizeof path, "%s/content/behaviors/Unsaved.chibi.meta", project);
    unlink(path);
    snprintf(path, sizeof path, "%s/content/behaviors", project);
    rmdir(path);
    snprintf(path, sizeof path, "%s/content", project);
    rmdir(path);
    rmdir(project);
}

static void system_format_preserves_source()
{
    const char
        *text = "system FreeMotion(position: Transform, velocity: Velocity) "
                "without Collider, Body {\n"
                "    // Integrate velocity without rewriting authored code.\n"
                "    position += velocity * dt;\n"
                "}\n";
    char project[] = "/tmp/chibi-system-format-XXXXXX";
    test_check(mkdtemp(project) != NULL, "create structured system formatting project");
    char path[512];
    snprintf(path, sizeof path, "%s/content", project);
    test_check(mkdir(path, 0700) == 0, "create system formatting content directory");
    snprintf(path, sizeof path, "%s/content/systems", project);
    test_check(mkdir(path, 0700) == 0, "create system formatting asset directory");
    snprintf(path, sizeof path, "%s/content/systems/FreeMotion.chibi", project);
    FILE *file = fopen(path, "wb");
    test_check(file != NULL, "create authored system source");
    if (file) {
        fwrite(text, 1, strlen(text), file);
        fclose(file);
    }
    code_source *authored = code_source_parse(text);
    test_check(authored->asset != NULL, "concise system source parses before formatting");
    if (authored->asset) {
        node *placed = authored->asset->graph->nodes[0];
        placed->placed = true;
        placed->x = 120;
        placed->y = 80;
        test_check(code_source_metadata_save(authored, path), "save optional authored system presentation metadata");
    }
    test_check(chibi_automation_format_assets(project, true) == 0, "canonical check accepts concise authored system text");
    test_check(chibi_automation_format_assets(project, false) == 0, "format preserves structured system source");
    char *formatted = text_file_read(path);
    test_check(formatted && !strcmp(formatted, text), "system format preserves compound assignment, header and comments exactly");
    code_source *reopened = formatted ? code_source_parse(formatted) : NULL;
    test_check(reopened && code_source_metadata_load(reopened, path), "formatted system retains optional source metadata");
    test_check(
        authored->asset && reopened && reopened->asset &&
            behavior_asset_equal(authored->asset, reopened->asset),
        "formatted system retains the complete native graph and presentation"
    );
    code_source_free(reopened);
    code_source_free(authored);
    free(formatted);
    unlink(path);
    char sidecar[540];
    snprintf(sidecar, sizeof sidecar, "%s.meta", path);
    unlink(sidecar);
    snprintf(path, sizeof path, "%s/content/systems", project);
    rmdir(path);
    snprintf(path, sizeof path, "%s/content", project);
    rmdir(path);
    rmdir(project);
}

static void project_library_context()
{
    char project[] = "/tmp/chibi-code-api-catalog-XXXXXX";
    test_check(mkdtemp(project) != NULL, "create source API project catalogue fixture");
    char path[512];
    const char *folders[] = { "content", "content/libraries", "content/behaviors" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", project, folders[i]);
        test_check(mkdir(path, 0700) == 0, "create source API project catalogue directory");
    }
    snprintf(path, sizeof path, "%s/content/libraries/Shared.chibi", project);
    FILE *file = fopen(path, "wb");
    test_check(file != NULL, "create source API library definition");
    if (file) {
        fputs("library Shared { pure function Seven() -> (Result: int) { return 7 } }", file);
        fclose(file);
    }
    const char *source = "behavior Unsaved { value chosen = Shared.Seven(); }";
    int status;
    yyjson_doc *response = analyze_project(project, source, (int)(strstr(source, "Seven") - source), &status);
    yyjson_val *result = yyjson_obj_get(yyjson_doc_get_root(response), "result");
    test_check(status == CHIBI_AUTOMATION_OK && yyjson_get_bool(yyjson_obj_get(result, "valid")), "analyzeCode resolves project library signatures without authored call pins");
    if (status != CHIBI_AUTOMATION_OK ||
        !yyjson_get_bool(yyjson_obj_get(result, "valid"))) {
        char *failure = yyjson_write(response, 0, NULL);
        fprintf(stderr, "project library analysis: %s\n", failure);
        free(failure);
    }
    yyjson_val *definition = yyjson_obj_get(result, "definition");
    test_check(
        definition && yyjson_is_str(yyjson_obj_get(definition, "asset")) &&
            !strcmp(yyjson_get_str(yyjson_obj_get(definition, "asset")), "Shared") &&
            yyjson_get_uint(yyjson_obj_get(definition, "graph_id")) != 0,
        "analyzeCode external definition includes exact library identity"
    );
    yyjson_doc_free(response);
    snprintf(path, sizeof path, "%s/content/behaviors/Unsaved.chibi", project);
    write_source(path, source);
    char *reply = chibi_automation_handle(
        "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":"
        "\"behavior\",\"name\":\"Unsaved\"}",
        project,
        &status
    );
    test_check(status == CHIBI_AUTOMATION_OK, "validateAsset resolves authored calls against project library bodies");
    free(reply);
    char *saved = text_file_read(path);
    test_check(saved && strstr(saved, "Shared.Seven"), "file authoring retains readable external call source");
    free(saved);
    unlink(path);
    snprintf(path, sizeof path, "%s/content/libraries/Shared.chibi", project);
    unlink(path);
    for (int i = 2; i >= 0; i--) {
        snprintf(path, sizeof path, "%s/%s", project, folders[i]);
        rmdir(path);
    }
    rmdir(project);
}

/* Empty executable bodies are also valid KDL syntax, so dispatch must use the
 * declared source language before interpreting their native document. */
static void empty_source_documents()
{
    char project[] = "/tmp/chibi-empty-code-XXXXXX";
    assert(mkdtemp(project));
    char content[256], folder[256], path[256], request[256];
    snprintf(content, sizeof content, "%s/content", project);
    assert(!mkdir(content, 0700));
    static const struct
    {
        const char *kind, *folder, *extension, *source;
    } cases[] = {
        { "behavior", "behaviors", "chibi", "behavior \"Empty\" {}" },
        { "library", "libraries", "chibi", "library \"Empty\" {}" },
        { "system", "systems", "chibi", "system \"Empty\" {}" },
        { "event", "events", "kdl", "name \"Empty\"\nversion 13\n" },
    };
    for (int i = 0; i < 4; i++) {
        snprintf(folder, sizeof folder, "%s/content/%s", project, cases[i].folder);
        assert(!mkdir(folder, 0700));
        snprintf(
            path,
            sizeof path,
            "%s/content/%s/Empty.%s",
            project,
            cases[i].folder,
            cases[i].extension
        );
        FILE *file = fopen(path, "wb");
        assert(file && fputs(cases[i].source, file) >= 0 && !fclose(file));
        snprintf(
            request,
            sizeof request,
            "{\"contract\":1,\"operation\":\"read_asset\",\"kind\":\"%s\","
            "\"name\":\"Empty\"}",
            cases[i].kind
        );
        int status;
        char *reply = chibi_automation_handle(request, project, &status);
        yyjson_doc *response = yyjson_read(reply, strlen(reply), 0);
        yyjson_val *document = yyjson_ptr_get(yyjson_doc_get_root(response), "/result/document");
        const char *name = yyjson_get_str(yyjson_obj_get(document, "name"));
        test_check(
            status == CHIBI_AUTOMATION_OK && name && !strcmp(name, "Empty") &&
                (i == 3 ? yyjson_get_int(yyjson_obj_get(document, "version")) == 13 &&
                              !yyjson_obj_get(document, "graphs")
                        : yyjson_is_arr(yyjson_obj_get(document, "graphs"))),
            "readAsset dispatches empty readable executable bodies natively "
            "and retains declarative KDL values"
        );
        yyjson_doc_free(response);
        free(reply);
        if (i < 3) {
            snprintf(
                request,
                sizeof request,
                "{\"contract\":1,\"operation\":\"%s\",\"kind\":\"%s\",\"name\":"
                "\"Empty\"}",
                i == 2 ? "inspect_system" : "inspect_graph",
                cases[i].kind
            );
            reply = chibi_automation_handle(request, project, &status);
            response = yyjson_read(reply, strlen(reply), 0);
            test_check(
                status == CHIBI_AUTOMATION_OK &&
                    yyjson_is_arr(yyjson_ptr_get(yyjson_doc_get_root(response), "/result/graphs")),
                "source inspection exposes native graphs for empty readable "
                "behaviors, libraries and systems"
            );
            yyjson_doc_free(response);
            free(reply);
        }
        assert(!unlink(path) && !rmdir(folder));
    }
    assert(!rmdir(content) && !rmdir(project));
}

static void retired_mutations_are_unknown()
{
    const char *operations[] = { "edit_graph",   "edit_prototype",
                                 "edit_scene",   "optimize_graph",
                                 "build_code",   "rename_symbol",
                                 "delete_symbol" };
    for (int i = 0; i < 7; i++) {
        char request[128];
        snprintf(request, sizeof request, "{\"contract\":1,\"operation\":\"%s\"}", operations[i]);
        int status;
        char *reply = chibi_automation_handle(request, "/missing-project-root", &status);
        test_check(
            status == CHIBI_AUTOMATION_INVALID_REQUEST && reply &&
                strstr(reply, "unknown operation"),
            "retired controller mutation is rejected before project access"
        );
        free(reply);
    }
}

void test_automation_code()
{
    retired_mutations_are_unknown();
    empty_source_documents();
    project_library_context();
    int nul_status;
    char *nul_response = chibi_automation_handle(
        "{\"contract\":1,\"operation\":\"analyze_code\",\"source\":\"behavior "
        "Safe {}\\u0000behavior Hidden {}\"}",
        "/missing-project-root",
        &nul_status
    );
    test_check(nul_status == CHIBI_AUTOMATION_INVALID_REQUEST, "source analysis rejects embedded NUL instead of silently truncating the candidate");
    free(nul_response);
    behavior_asset *asset = behavior_asset_new("Unsaved");
    char *source = code_source_format(asset);
    int status;
    yyjson_doc *response = analyze(source, 0, &status);
    yyjson_val *root = yyjson_doc_get_root(response);
    yyjson_val *result = yyjson_obj_get(root, "result");
    test_check(
        status == CHIBI_AUTOMATION_OK && yyjson_get_bool(yyjson_obj_get(root, "ok")),
        "unsaved code analysis does not require a project or modify files"
    );
    test_check(yyjson_get_bool(yyjson_obj_get(result, "valid")), "native source candidate compiles through controller");
    test_check(yyjson_is_obj(yyjson_obj_get(result, "document")), "source analysis exposes the native graph document");
    test_check(yyjson_is_str(yyjson_obj_get(result, "formatted")), "source analysis exposes canonical code");
    yyjson_doc_free(response);
    response = analyze(source, (int)strlen(source) + 1, &status);
    test_check(status == CHIBI_AUTOMATION_INVALID_REQUEST, "completion position beyond source is rejected");
    yyjson_doc_free(response);
    response = analyze("behavior {", -1, &status);
    result = yyjson_obj_get(yyjson_doc_get_root(response), "result");
    test_check(
        status == CHIBI_AUTOMATION_OK &&
            !yyjson_get_bool(yyjson_obj_get(result, "valid")),
        "incomplete source is an analysis result with a failed candidate"
    );
    yyjson_val *diagnostic = yyjson_arr_get(yyjson_obj_get(result, "diagnostics"), 0);
    test_check(
        diagnostic && yyjson_is_int(yyjson_obj_get(diagnostic, "start")) &&
            yyjson_is_int(yyjson_obj_get(diagnostic, "line")),
        "incomplete source diagnostics carry byte ranges and line positions"
    );
    yyjson_doc_free(response);
    system_format_preserves_source();
    persisted_source(source);
    persisted_source("behavior Unsaved {\n    // Keep this authored comment.\n    state int count = 0;\n    on Tick() {}\n}\n");
    persisted_source("behavior DeclaredName {\n    // Keep this authored comment.\n    state int count = 0;\n    on Tick() {}\n}\n");
    free(source);
    behavior_asset_free(asset);
}
