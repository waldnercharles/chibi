#include "editor/graph/search.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/events.h"
#include "test/suite_internal.h"

static pin_spec g_editor_float_outputs[] = {
    { "Value", PIN_F32, "float", PIN_DATA },
};
static pin_spec g_editor_float_inputs[] = {
    { "Value", PIN_F32, "float", PIN_DATA },
};
static pin_spec g_editor_int_inputs[] = {
    { "Value", PIN_I32, "int", PIN_DATA },
    { "Condition", PIN_BOOL, "bool", PIN_DATA },
};
static node_desc g_editor_float_sink_desc = {
    .name = "editor_float_sink",
    .input_count = 1,
    .inputs = g_editor_float_inputs,
};
static node_desc g_editor_float_desc = {
    .name = "editor_float_source",
    .output_count = 1,
    .outputs = g_editor_float_outputs,
};
static node_desc g_editor_int_desc = {
    .name = "editor_int_sink",
    .input_count = 2,
    .inputs = g_editor_int_inputs,
};

static int make_editor_save_path(char *path, size_t path_size)
{
#ifdef _WIN32
    char directory[MAX_PATH];
    DWORD directory_size = GetTempPathA(sizeof directory, directory);
    if (!directory_size || directory_size >= sizeof directory ||
        path_size < MAX_PATH || !GetTempFileNameA(directory, "cbp", 0, path)) {
        return -1;
    }
    return 0;
#else
    static const char template[] = "chibi-editor-XXXXXX";
    if (path_size < sizeof template) {
        return -1;
    }
    memcpy(path, template, sizeof template);
    int fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return 0;
#endif
}

static void search_popup_frame(editor *e, bool open)
{
    ImGui_NewFrame();
    ImGui_SetNextWindowSize((ImVec2){ 700, 500 }, ImGuiCond_Always);
    ImGui_Begin("Search interaction", NULL, 0);
    if (open) {
        ed_open_context_menu(e, (ImVec2){ 120, 120 });
    }
    ed_handle_popups(e);
    ImGui_End();
    ImGui_Render();
}

static void search_popup_insert(editor *e, const char *query)
{
    search_popup_frame(e, true);
    search_popup_frame(e, false);
    ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), query);
    search_popup_frame(e, false);
    search_popup_frame(e, false);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, true);
    search_popup_frame(e, false);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, false);
    search_popup_frame(e, false);
}

static void check_linked_preview()
{
    behavior_asset *library = behavior_asset_new("PreviewMath");
    library->domain = BEHAVIOR_DOMAIN_LIBRARY;
    graph *identity = behavior_asset_add_function(library, "Identity", 1);
    pin_spec value = { .name = "Value", .type = PIN_F64, .type_name = "double", .kind = PIN_DATA };
    node *entry = graph_node_by_id(identity, identity->entry_id);
    node *ret = graph_node_by_id(identity, identity->return_id);
    assert(graph_add_pin(identity, entry, 1, &value) == 0);
    assert(graph_add_pin(identity, ret, 0, &value) == 0);
    assert(graph_connect_data(identity, entry, 0, ret, 0) == 0);

    behavior_asset *asset = behavior_asset_new("Linked preview");
    node *number = graph_add_node(asset->graph, registry_node_by_name("Real"));
    assert(
        graph_set_pin_default(asset->graph, number, 0, 0, &(pin_value){ .type = PIN_F64, .d = 7 }) == 0
    );
    node *call = behavior_asset_add_function_call_node_to_graph(asset, asset->graph, identity);
    assert(call->reference_asset);
    assert(graph_connect_data(asset->graph, number, 0, call, 0) == 0);
    const behavior_asset *catalog[] = { library };
    eval_view *view = eval_view_new();
    eval_view_set_catalog(view, catalog, 1);
    eval_view_refresh(view, asset->graph);
    test_check(!eval_view_error(view)[0], "preview links authored library references before compilation");
    test_check(!strcmp(eval_view_output(view, number, 0), "7"), "linked preview maps values back to authored nodes");

    assert(
        graph_set_pin_default(asset->graph, number, 0, 0, &(pin_value){ .type = PIN_F64, .d = 11 }) == 0
    );
    eval_view_refresh(view, asset->graph);
    test_check(
        !eval_view_error(view)[0] && !strcmp(eval_view_output(view, number, 0), "11"),
        "preview replaces its linked generation after graph edits"
    );
    eval_view_set_catalog(view, NULL, 0);
    eval_view_refresh(view, asset->graph);
    test_check(
        strstr(eval_view_error(view), "PreviewMath") != NULL,
        "preview reports a missing library instead of hiding compilation errors"
    );
    eval_view_set_catalog(view, catalog, 1);
    eval_view_refresh(view, asset->graph);
    test_check(
        !eval_view_error(view)[0] && !strcmp(eval_view_output(view, number, 0), "11"),
        "preview recovers when its library returns to the catalog"
    );
    eval_view_free(view);
    behavior_asset_free(asset);
    behavior_asset_free(library);
}

static void check_search_catalog()
{
    behavior_asset *asset = behavior_asset_new("Search catalog");
    graph *function = behavior_asset_add_function(asset, "ComputeVelocity", 1);
    graph *macro = behavior_asset_add_macro(asset, "RepeatStep");
    behavior_variable *variable = behavior_asset_add_variable(
        asset,
        "TrailBudget",
        PIN_F64,
        NULL,
        PIN_CONTAINER_SINGLE,
        NULL,
        NULL
    );
    behavior_asset *library = behavior_asset_new("MotionLibrary");
    library->domain = BEHAVIOR_DOMAIN_LIBRARY;
    graph *export = behavior_asset_add_function(library, "LimitSpeed", 1);
    behavior_asset *event_definition = behavior_asset_new("Impact");
    event_definition->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    behavior_asset_add_variable(event_definition, "Strength", PIN_F64, NULL, PIN_CONTAINER_SINGLE, NULL, NULL);
    editor *e = editor_new(asset);
    const behavior_asset *assets[] = { library, event_definition };
    const char *prototypes[] = { "TrailParticle" };
    editor_set_asset_catalog(e, assets, 2);
    editor_set_prototype_catalog(e, prototypes, 1);
    const char *queries[] = { "Compute Velocity",
                              "Repeat Step",
                              "Get Trail Budget",
                              "Set Trail Budget",
                              "MotionLibrary / LimitSpeed",
                              "Spawn TrailParticle",
                              "On Impact",
                              "Call Impact",
                              "Send Impact" };
    const char *fuzzy_queries[] = { "velocity compute",
                                    "CV",
                                    "compute velcoity",
                                    "budget trail get",
                                    "math formula" };
    const char *fuzzy_names[] = { "Compute Velocity",
                                  "Compute Velocity",
                                  "Compute Velocity",
                                  "Get Trail Budget",
                                  "Math Expression" };
    for (int i = 0; i < 5; i++) {
        ed_search_match *matches = ed_search_matches(e, fuzzy_queries[i]);
        test_check(acount(matches) > 0 && !strcmp(matches[0].name, fuzzy_names[i]), "ranked node search handles reordered words, initials and transposed letters");
        afree(matches);
    }
    node *expected[9] = { 0 };
    for (int i = 0; i < 9; i++) {
        ed_search_match *matches = ed_search_matches(e, queries[i]);
        test_check(
            acount(matches) > 0 && strcmp(matches[0].name, queries[i]) == 0,
            "search ranks an exact authored member ahead of fuzzy node matches"
        );
        node *n = acount(matches) ? ed_spawn_match(e, &matches[0]) : NULL;
        expected[i] = n;
        test_check(n != NULL, "search results create configured authored nodes");
        if (n && i < 2) {
            test_check(
                n->reference_id == (i ? macro->graph_id : function->graph_id),
                "local search calls preserve graph identity"
            );
        }
        if (n && (i == 2 || i == 3)) {
            test_check(n->reference_id == variable->id, "variable search binds the chosen variable");
        }
        if (n && i == 4) {
            test_check(
                n->reference_id == export->graph_id && n->reference_asset &&
                    !strcmp(n->reference_asset, library->name),
                "library search binds the library function"
            );
        }
        if (n && i == 5) {
            test_check(
                n->input_pins[1].default_value.str &&
                    !strcmp(n->input_pins[1].default_value.str, prototypes[0]),
                "prototype search preselects the spawn asset"
            );
        }
        if (n && i >= 6) {
            test_check(graph_event_matches(n, event_definition), "event search binds the typed payload signature");
        }
        afree(matches);
    }
    node *source = graph_add_node(asset->graph, &g_editor_float_desc);
    node *sink = graph_add_node(asset->graph, &g_editor_float_sink_desc);
    ImGuiIO *io = ImGui_GetIO();
    io->DisplaySize = (ImVec2){ 800, 600 };
    io->DeltaTime = 1.0f / 60.0f;
    ImFontAtlas_Build(io->Fonts);
    ImGui_NewFrame();
    ImVec2 disconnected = ed_node_dimensions(e, asset->graph, sink);
    graph_connect(asset->graph, source, 0, sink, 0);
    ImVec2 connected = ed_node_dimensions(e, asset->graph, sink);
    test_check(
        disconnected.x == connected.x && disconnected.y == connected.y,
        "connecting an input preserves node dimensions and pin locations"
    );
    ImGui_EndFrame();
    for (int i = 0; i < 9; i++) {
        int count = acount(asset->graph->nodes);
        search_popup_insert(e, queries[i]);
        node *inserted = acount(asset->graph->nodes) == count + 1
                           ? asset->graph->nodes[count]
                           : NULL;
        test_check(
            inserted && expected[i] && inserted->desc == expected[i]->desc &&
                inserted->reference_id == expected[i]->reference_id,
            "typing an authored member in the context popup and pressing Enter "
            "inserts the selected symbol once"
        );
    }
    node *event = graph_add_node(asset->graph, &NODE_Event);
    ed_snapshot_before(e);
    e->spawn_link_pending = true;
    e->spawn_link_pin = (ed_pin_view){ event, ED_PIN_OUTPUT, 0, { 0, 0 } };
    ed_search_match *pure_matches = ed_search_matches(e, "Compute Velocity");
    bool has_pure = false;
    for (int i = 0; i < acount(pure_matches); i++) {
        has_pure |= pure_matches[i].function == function;
    }
    test_check(!has_pure, "exec pin search excludes a pure function with no exec input");
    afree(pure_matches);
    int edge_count = acount(asset->graph->edges);
    search_popup_insert(e, "Repeat Step");
    test_check(acount(asset->graph->edges) == edge_count + 1, "pin-drag popup searches macros and connects the compatible execution input");
    const editor_event_param input_params[] = { { "Move", PIN_F64, NULL } };
    const editor_event_signature host_events[] = { { "Input", input_params, 1 } };
    editor_set_event_catalog(e, host_events, 1);
    int host_count = acount(asset->graph->nodes);
    search_popup_insert(e, "On Input");
    node *input = acount(asset->graph->nodes) == host_count + 1
                    ? asset->graph->nodes[host_count]
                    : NULL;
    test_check(
        input && input->desc == &NODE_Event && input->reference_name &&
            !strcmp(input->reference_name, "Input") && input->output_count == 2 &&
            !strcmp(input->output_pins[1].name, "Move"),
        "context search inserts named engine events with the declared payload"
    );
    behavior_asset *shader_library = behavior_asset_new("ShaderMath");
    shader_library->domain = BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    graph *pulse = behavior_asset_add_function(shader_library, "Pulse", 1);
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    const behavior_asset *shader_assets[] = { shader_library };
    editor_set_asset_catalog(e, shader_assets, 1);
    ed_search_match *shader_matches = ed_search_matches(e, "ShaderMath / Pulse");
    node *shader_call = acount(shader_matches)
                          ? ed_spawn_match(e, &shader_matches[0])
                          : NULL;
    test_check(
        shader_call && shader_call->desc == &NODE_CallPureFunction &&
            shader_call->reference_id == pulse->graph_id && shader_call->reference_asset &&
            !strcmp(shader_call->reference_asset, "ShaderMath"),
        "shader search finds and inserts pure library functions by their "
        "qualified names"
    );
    afree(shader_matches);
    behavior_asset_free(shader_library);
    editor_free(e);
    behavior_asset_free(library);
    behavior_asset_free(event_definition);
}

static void check_library_search_snapshot()
{
    behavior_asset *catalog = behavior_asset_new("LibrarySnapshot");
    catalog->domain = BEHAVIOR_DOMAIN_LIBRARY;
    behavior_asset_add_function(catalog, "CurrentFunction", 1);
    behavior_asset_add_function(catalog, "SiblingFunction", 1);
    char *json = behavior_asset_save_json(catalog);
    behavior_asset *snapshot = behavior_asset_load_json(json);
    free(json);
    test_check(snapshot != NULL, "library search fixture loads an independent working snapshot");
    if (!snapshot) {
        behavior_asset_free(catalog);
        return;
    }
    editor *e = editor_new(snapshot);
    const behavior_asset *assets[] = { catalog };
    editor_set_asset_catalog(e, assets, 1);
    graph *current = behavior_asset_find_graph_by_name(snapshot, "CurrentFunction");
    graph *sibling = behavior_asset_find_graph_by_name(snapshot, "SiblingFunction");
    editor_open_graph(e, current->graph_id);
    ed_search_match *matches = ed_search_matches(e, "");
    int functions = 0;
    const ed_search_match *local = NULL;
    for (int i = 0; i < acount(matches); i++) {
        if (!matches[i].function) {
            continue;
        }
        functions++;
        if (matches[i].function == sibling) {
            local = &matches[i];
        }
    }
    test_check(functions == 1 && local, "library search excludes the current function and duplicate catalog snapshot functions");
    node *call = local ? ed_spawn_match(e, local) : NULL;
    test_check(call && call->reference_id == sibling->graph_id && !call->reference_asset, "library search inserts the sibling from the working document as a local call");
    afree(matches);
    editor_free(e);
    behavior_asset_free(catalog);
}

void run_editor_command_checks(void)
{
    ImGuiContext *context = ImGui_CreateContext(NULL);
    editor_ui_initialize(false);
    check_linked_preview();
    check_search_catalog();
    check_library_search_snapshot();
    char save_path[512];
    if (make_editor_save_path(save_path, sizeof save_path) != 0) {
        test_check(0, "a temporary path can be generated for editor persistence");
        ImGui_DestroyContext(context);
        return;
    }
    behavior_asset *asset = behavior_asset_new("Editor command test");
    node *source = graph_add_node(asset->graph, &g_editor_float_desc);
    node *sink = graph_add_node(asset->graph, &g_editor_int_desc);
    node *float_sink = graph_add_node(asset->graph, &g_editor_float_sink_desc);
    editor *e = editor_new(asset);
    test_check(e != NULL, "editor command seam constructs a headless editor");
    if (!e) {
        behavior_asset_free(asset);
        remove(save_path);
        ImGui_DestroyContext(context);
        return;
    }

    editor_query state;
    editor_query_state(e, &state);
    test_check(!state.dirty, "a new editor starts with a clean saved baseline");
    editor_tick(e, 0.0);
    editor_command invalid_connect = {
        .kind = EDITOR_COMMAND_CONNECT_DATA,
        .data.connect = { source->id, 0, sink->id, 1 },
    };
    test_check(editor_command_apply(e, invalid_connect) == -1, "an invalid connection is rejected by the command seam");
    editor_query_state(e, &state);
    test_check(!state.dirty && acount(asset->graph->edges) == 0, "a rejected connection leaves no dirty or history edit");
    unsigned revision = editor_edit_revision(e);
    editor_undo(e);
    editor_tick(e, 1.0);
    test_check(editor_edit_revision(e) == revision, "a rejected connection does not add an undo revision");
    unsigned revision_before_invalid_spawn = editor_edit_revision(e);
    test_check(
        editor_command_apply(
            e,
            (editor_command){
                .kind = EDITOR_COMMAND_SPAWN_NODE,
                .data.spawn_node = { NULL, 0.0f, 0.0f },
            }
        ) == -1,
        "a spawn command rejects a missing node description"
    );
    test_check(
        editor_edit_revision(e) == revision_before_invalid_spawn &&
            acount(asset->graph->nodes) == 3,
        "a rejected spawn leaves no undo revision or node"
    );
    pin_value float_value = { .type = PIN_F32, .f = 1.0f };
    editor_query_state(e, &state);
    int dirty_before_invalid_pin = state.dirty;
    unsigned revision_before_invalid_pin = editor_edit_revision(e);
    test_check(
        editor_command_apply(
            e,
            (editor_command){
                .kind = EDITOR_COMMAND_SET_PIN_DEFAULT,
                .data.set_pin_default = { source->id, 2, 0, &float_value },
            }
        ) == -1,
        "a pin default command rejects a non-boolean output selector"
    );
    test_check(
        editor_command_apply(
            e,
            (editor_command){
                .kind = EDITOR_COMMAND_SET_PIN_DEFAULT,
                .data.set_pin_default = { source->id, 0, 1, &float_value },
            }
        ) == -1,
        "a pin default command rejects an out-of-range pin"
    );
    test_check(
        editor_command_apply(
            e,
            (editor_command){
                .kind = EDITOR_COMMAND_SET_PIN_DEFAULT,
                .data.set_pin_default = { sink->id, 0, 0, &float_value },
            }
        ) == -1,
        "a pin default command rejects a mismatched value type"
    );
    editor_query_state(e, &state);
    test_check(
        state.dirty == dirty_before_invalid_pin &&
            editor_edit_revision(e) == revision_before_invalid_pin,
        "rejected pin defaults leave no dirty or history edit"
    );
    editor_command valid_connect = {
        .kind = EDITOR_COMMAND_CONNECT_DATA,
        .data.connect = { source->id, 0, float_sink->id, 0 },
    };
    test_check(editor_command_apply(e, valid_connect) == 0, "a valid typed connection uses the editor mutation path");
    test_check(acount(asset->graph->edges) == 1, "valid command connections update graph topology");

    editor_command create_graph = {
        .kind = EDITOR_COMMAND_CREATE_GRAPH,
        .data.create_graph = { GRAPH_FUNCTION, "Worker" },
    };
    editor_query_state(e, &state);
    int dirty_before_create = state.dirty;
    unsigned revision_before_create = editor_edit_revision(e);
    test_check(
        editor_command_apply(
            e,
            (editor_command){
                .kind = EDITOR_COMMAND_CREATE_GRAPH,
                .data.create_graph = { GRAPH_FUNCTION, "" },
            }
        ) == -1,
        "graph creation rejects an empty name"
    );
    editor_query_state(e, &state);
    test_check(
        state.dirty == dirty_before_create &&
            editor_edit_revision(e) == revision_before_create,
        "a rejected graph creation leaves no dirty or history edit"
    );
    test_check(editor_command_apply(e, create_graph) == 0, "the command seam creates a graph");
    graph *worker = behavior_asset_find_graph_by_name(asset, "Worker");
    test_check(worker != NULL, "created graphs remain discoverable by name");
    test_check(
        worker && acount(worker->nodes) == 2 &&
            worker->nodes[0]->desc == &NODE_FunctionEntry &&
            worker->nodes[1]->desc == &NODE_Return,
        "command-created functions receive entry and return nodes"
    );
    test_check(
        editor_open_graph(e, worker->graph_id) == 0 && editor_active_graph(e) == worker,
        "the command seam can open the created graph"
    );

    editor_query_state(e, &state);
    int dirty_before_rename = state.dirty;
    unsigned revision_before_rename = editor_edit_revision(e);
    test_check(
        editor_command_apply(
            e,
            (editor_command){
                .kind = EDITOR_COMMAND_RENAME_GRAPH,
                .data.rename_graph = { worker->graph_id, "" },
            }
        ) == -1,
        "graph renaming rejects an empty name"
    );
    editor_query_state(e, &state);
    test_check(
        state.dirty == dirty_before_rename &&
            editor_edit_revision(e) == revision_before_rename,
        "a rejected graph rename leaves no dirty or history edit"
    );

    editor_command rename_graph = {
        .kind = EDITOR_COMMAND_RENAME_GRAPH,
        .data.rename_graph = { worker->graph_id, "Renamed" },
    };
    test_check(editor_command_apply(e, rename_graph) == 0, "the command seam renames a graph");
    /* The local seam fixtures are not registry descriptors, so remove them
     * before exercising the file round trip. */
    graph_remove_node(asset->graph, source);
    graph_remove_node(asset->graph, sink);
    graph_remove_node(asset->graph, float_sink);
    editor_query_state(e, &state);
    test_check(state.dirty, "authored graph changes are observable as dirty");
    test_check(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_SAVE, .data.file = { save_path } }
        ) == 0,
        "the command seam saves the authored asset"
    );
    editor_query_state(e, &state);
    test_check(!state.dirty, "saving establishes the clean dirty baseline");
    test_check(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_LOAD, .data.file = { save_path } }
        ) == 0,
        "the command seam reloads the saved asset"
    );
    editor_query_state(e, &state);
    test_check(
        state.graph_count == 2 && state.active_graph &&
            strcmp(state.active_graph->name, "Renamed") == 0 && !state.dirty,
        "reload preserves authored graphs and the clean dirty baseline"
    );
    behavior_asset *other = behavior_asset_new("Other asset");
    test_check(
        other && editor_open_behavior_asset(e, other, other->graph->graph_id) == 0,
        "opening a different behavior asset succeeds"
    );
    editor_query_state(e, &state);
    test_check(state.graph_count == 1 && !state.dirty, "opening a different clean behavior asset resets the dirty baseline");
    test_check(
        editor_command_apply(
            e,
            (editor_command){
                .kind = EDITOR_COMMAND_CREATE_GRAPH,
                .data.create_graph = { GRAPH_FUNCTION, "OtherWorker" },
            }
        ) == 0,
        "the newly opened behavior asset remains editable"
    );
    editor_query_state(e, &state);
    test_check(state.dirty, "edits after opening a different asset are observable as dirty");
    behavior_asset_free(other);
    test_check(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_SAVE, .data.file = { NULL } }
        ) == -1,
        "a save command rejects a null path"
    );
    test_check(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_LOAD, .data.file = { NULL } }
        ) == -1,
        "a load command rejects a null path"
    );
    remove(save_path);
    editor_free(e);
    ImGui_DestroyContext(context);
}
