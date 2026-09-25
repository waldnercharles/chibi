#include "editor/graph/search.h"
#include "editor/panels/field_details.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/event_catalog.h"
#include "graph/document/events.h"
#include "graph/nodes/component_nodes.h"
#include "test/suite_internal.h"

static pin_spec g_editor_float_outputs[] = {
    { "Value", PIN_F32, "float", PIN_DATA },
};
static pin_spec g_editor_vector_outputs[] = {
    { "Value", PIN_STRUCT, "CF_V2", PIN_DATA },
};
static pin_spec g_editor_float_inputs[] = {
    { "Value", PIN_F32, "float", PIN_DATA },
};
static pin_spec g_editor_double_inputs[] = {
    { "Value", PIN_F64, "double", PIN_DATA },
};
static pin_spec g_editor_vector_inputs[] = {
    { "Value", PIN_STRUCT, "CF_V2", PIN_DATA },
};
static pin_spec g_editor_vector_constructor_inputs[] = {
    { "X", PIN_F32, "float", PIN_DATA },
    { "Y", PIN_F32, "float", PIN_DATA },
};
static pin_spec g_editor_int_inputs[] = {
    { "Value", PIN_I32, "int", PIN_DATA },
    { "Condition", PIN_BOOL, "bool", PIN_DATA },
};
static pin_spec g_editor_name_inputs[] = {
    { "Name", PIN_NAME, "name", PIN_DATA },
};
static pin_spec g_editor_single_value_inputs[] = {
    { "Integer", PIN_I32, "int", PIN_DATA },
    { "UInt", PIN_U32, "uint", PIN_DATA },
    { "Enum", PIN_ENUM, "Mode", PIN_DATA },
    { "String", PIN_STR, "const char *", PIN_DATA },
    { "Name", PIN_NAME, "name", PIN_DATA },
};
static pin_spec g_editor_multi_value_inputs[] = {
    { "Vector", PIN_STRUCT, "CF_V2", PIN_DATA },
    { "Color", PIN_U32, "uint", PIN_DATA, .flags = PIN_FLAG_COLOR },
    { "Array", PIN_I32, "int", PIN_DATA, .container = PIN_CONTAINER_ARRAY },
    { "Struct", PIN_STRUCT, "CustomStruct", PIN_DATA },
    { "Flags", PIN_U32, "uint", PIN_DATA },
};
static node_desc g_editor_float_sink_desc = {
    .name = "editor_float_sink",
    .input_count = 1,
    .inputs = g_editor_float_inputs,
};
static node_desc g_editor_double_sink_desc = {
    .name = "editor_double_sink",
    .input_count = 1,
    .inputs = g_editor_double_inputs,
};
static node_desc g_editor_float_desc = {
    .name = "editor_float_source",
    .output_count = 1,
    .outputs = g_editor_float_outputs,
};
static node_desc g_editor_float_relay_desc = {
    .name = "editor_float_relay",
    .input_count = 1,
    .output_count = 1,
    .inputs = g_editor_float_inputs,
    .outputs = g_editor_float_outputs,
};
static node_desc g_editor_name_relay_desc = {
    .name = "editor_name_relay",
    .input_count = 1,
    .output_count = 1,
    .inputs = g_editor_name_inputs,
    .outputs = g_editor_float_outputs,
};
static node_desc g_editor_vector_desc = {
    .name = "editor_vector_source",
    .output_count = 1,
    .outputs = g_editor_vector_outputs,
};
static node_desc g_editor_vector_sink_desc = {
    .name = "editor_vector_sink",
    .input_count = 1,
    .inputs = g_editor_vector_inputs,
};
static node_desc g_editor_vector_constructor_desc = {
    .name = "editor_vector_constructor",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_editor_vector_constructor_inputs,
    .outputs = g_editor_vector_outputs,
    .pure_kind = NODE_PURE_MAKE,
};
static node_desc g_editor_int_desc = {
    .name = "editor_int_sink",
    .input_count = 2,
    .inputs = g_editor_int_inputs,
};
static node_desc g_editor_single_value_desc = {
    .name = "editor_single_value_sink",
    .input_count = 5,
    .inputs = g_editor_single_value_inputs,
};
static node_desc g_editor_multi_value_desc = {
    .name = "editor_multi_value_sink",
    .input_count = 5,
    .inputs = g_editor_multi_value_inputs,
};
static const editor_flag g_editor_flags[] = {
    { 1u, "One" },
};
static const editor_flags g_editor_flag_catalog = {
    g_editor_flags,
    1,
};

static const editor_flags *editor_test_flags(const node *n, int output, int pin, void *user)
{
    (void)user;
    return !output && n->desc == &g_editor_multi_value_desc && pin == 4
             ? &g_editor_flag_catalog
             : NULL;
}

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

static bool direct_pin_drag(editor *e, ed_pin_view source, ed_pin_view target)
{
    ImGuiIO *io = ImGui_GetIO();
    aclear(e->pins);
    apush(e->pins, target);
    e->linking = true;
    e->link_pin = source;
    e->pending_snapshot = ed_snapshot(e);
    io->DisplaySize = (ImVec2){ 800, 600 };
    io->DeltaTime = 1.0f / 60.0f;
    ImGuiIO_AddMousePosEvent(io, target.pos.x, target.pos.y);
    ImGuiIO_AddMouseButtonEvent(io, 0, true);
    ImGui_NewFrame();
    ed_handle_input(e);
    ImGui_EndFrame();
    ImGuiIO_AddMouseButtonEvent(io, 0, false);
    ImGui_NewFrame();
    ed_handle_input(e);
    ImGui_EndFrame();
    return !e->linking;
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

/* A node dropped on a wire joins it in one step, and one that can't take
   the wire's value leaves the wire as it was. */
static void check_insert_on_wire()
{
    behavior_asset *asset = behavior_asset_new("Insert on wire");
    editor *e = editor_new(asset);
    graph *g = asset->graph;
    node *source = graph_add_node(g, &g_editor_float_desc);
    node *sink = graph_add_node(g, &g_editor_float_sink_desc);
    node *relay = graph_add_node(g, &g_editor_float_relay_desc);
    node *named = graph_add_node(g, &g_editor_name_relay_desc);
    assert(e && graph_connect_data(g, source, 0, sink, 0) == 0);
    node *into_sink = NULL, *into_relay = NULL;
    int pin = -1;
    bool refused = !ed_insert_on_wire(e, 0, named, 0, 0);
    test_check(
        refused && acount(g->edges) == 1 &&
            graph_input_source(g, sink, 0, &into_sink, &pin) && into_sink == source,
        "a node that can't take a wire's value leaves the wire unchanged"
    );
    bool inserted = ed_insert_on_wire(e, 0, relay, 0, 0);
    test_check(
        inserted && acount(g->edges) == 2 &&
            graph_input_source(g, relay, 0, &into_relay, &pin) && into_relay == source &&
            graph_input_source(g, sink, 0, &into_sink, &pin) && into_sink == relay,
        "inserting a node on a wire routes the wire through it"
    );
    editor_free(e);
}

static void check_inline_node_layout()
{
    behavior_asset *asset = behavior_asset_new("Inline node layout");
    editor *e = editor_new(asset);
    node *source = asset ? graph_add_node(asset->graph, &g_editor_vector_desc) : NULL;
    node *target = asset ? graph_add_node(asset->graph, &g_editor_vector_sink_desc)
                         : NULL;
    node *constructor = asset ? graph_add_node(asset->graph, &g_editor_vector_constructor_desc)
                              : NULL;
    node *constructor_target = asset ? graph_add_node(asset->graph, &g_editor_vector_sink_desc)
                                     : NULL;
    node *scalar_source = asset ? graph_add_node(asset->graph, &g_editor_float_desc)
                                : NULL;
    node *scalar_target = asset ? graph_add_node(asset->graph, &g_editor_float_sink_desc)
                                : NULL;
    bool vector_compact = false;
    bool constructor_rows = false;
    bool scalar_compact = false;
    if (e && source && target) {
        assert(graph_connect_data(asset->graph, source, 0, target, 0) == 0);
        assert(graph_set_node_presentation(asset->graph, source, NODE_PRESENTATION_INLINE) == 0);
        vector_compact = ed_pin_rows(e, asset->graph, target, ED_PIN_INPUT, 0) == 1;
    }
    if (e && constructor && constructor_target) {
        assert(graph_connect_data(asset->graph, constructor, 0, constructor_target, 0) == 0);
        assert(graph_set_node_presentation(asset->graph, constructor, NODE_PRESENTATION_INLINE) == 0);
        constructor_rows = ed_pin_rows(e, asset->graph, constructor_target, ED_PIN_INPUT, 0) >
                           1;
    }
    if (e && scalar_source && scalar_target) {
        assert(graph_connect_data(asset->graph, scalar_source, 0, scalar_target, 0) == 0);
        assert(
            graph_set_node_presentation(asset->graph, scalar_source, NODE_PRESENTATION_INLINE) == 0
        );
        scalar_compact = ed_pin_rows(e, asset->graph, scalar_target, ED_PIN_INPUT, 0) == 1;
    }
    test_check(vector_compact, "vector-valued inline sources share the target label row");
    test_check(constructor_rows, "inline vector constructors retain multiple value rows");
    test_check(scalar_compact, "scalar inline nodes share the target label row");
    if (e) {
        editor_free(e);
    } else {
        behavior_asset_free(asset);
    }
}

/* Wires shared into two new Adds; the first also folds its own Real. */
static node *add_shared_consumers(graph *g, node *shared, node **second)
{
    node *folded = graph_add_node(g, registry_node_by_name("Real"));
    node *first = graph_add_node(g, registry_node_by_name("Add"));
    *second = graph_add_node(g, registry_node_by_name("Add"));
    assert(graph_connect_data(g, shared, 0, first, 0) == 0);
    assert(graph_connect_data(g, shared, 0, *second, 0) == 0);
    assert(graph_connect_data(g, folded, 0, first, 1) == 0);
    return first;
}

static void check_delete_shared_source()
{
    behavior_asset *asset = behavior_asset_new("Delete shared source");
    graph *g = asset->graph;
    node *shared = graph_add_node(g, registry_node_by_name("Real"));
    editor *e = editor_new(asset);
    node *second = NULL;
    node *first = add_shared_consumers(g, shared, &second);
    ed_select_node(e, first, false);
    ed_select_node(e, second, true);
    ed_delete_selected(e);
    test_check(acount(g->nodes) == 1 && g->nodes[0] == shared, "deleting consumers keeps the source they shared");

    first = add_shared_consumers(g, shared, &second);
    ed_link_ref link = { shared->id,
                         shared->output_pins[0].id,
                         second->id,
                         second->input_pins[0].id };
    ed_select_node(e, first, false);
    ed_add_selected_link(e, &link);
    ed_delete_selected(e);
    test_check(
        acount(g->nodes) == 2 && g->nodes[0] == shared && g->nodes[1] == second,
        "deleting a consumer and a wire keeps the source they shared"
    );
    editor_free(e);
}

static void check_value_layout()
{
    behavior_asset *asset = behavior_asset_new("Value layout");
    editor *e = editor_new(asset);
    node *single = asset ? graph_add_node(asset->graph, &g_editor_single_value_desc)
                         : NULL;
    node *multi = asset ? graph_add_node(asset->graph, &g_editor_multi_value_desc)
                        : NULL;
    bool single_rows = false;
    bool multi_rows = false;
    if (e && single && multi) {
        editor_set_flags_resolver(e, editor_test_flags, NULL);
        single_rows = true;
        for (int pin = 0; pin < single->input_count; pin++) {
            single_rows &= ed_pin_rows(e, asset->graph, single, ED_PIN_INPUT, pin) == 1;
        }
        multi_rows = true;
        for (int pin = 0; pin < multi->input_count; pin++) {
            multi_rows &= ed_pin_rows(e, asset->graph, multi, ED_PIN_INPUT, pin) > 1;
        }
    }
    test_check(single_rows, "single-value inputs share the target label row");
    test_check(multi_rows, "multi-value inputs retain multiple value rows");
    if (e) {
        editor_free(e);
    } else {
        behavior_asset_free(asset);
    }
}

static void check_context_layout()
{
    behavior_asset *asset = behavior_asset_new("Context layout");
    editor *e = editor_new(asset);
    const node_desc *fractal_desc = registry_node_by_name("Shader Fractal Noise");
    node *fractal = asset && fractal_desc ? graph_add_node(asset->graph, fractal_desc)
                                          : NULL;
    bool compact = false;
    if (e && fractal) {
        compact = ed_pin_rows(e, asset->graph, fractal, ED_PIN_INPUT, 0) == 1;
    }
    test_check(compact, "contextual vector inputs share the target label row");
    if (e) {
        editor_free(e);
    } else {
        behavior_asset_free(asset);
    }
}

static void check_bound_scalar_layout()
{
    behavior_asset *asset = behavior_asset_new("Bound scalar layout");
    editor *e = editor_new(asset);
    node *target = asset ? graph_add_node(asset->graph, &g_editor_double_sink_desc)
                         : NULL;
    pin_binding binding = { .kind = PIN_BINDING_COMPONENT_FIELD,
                            .component_key = "GAME.GameOrientation",
                            .field_name = "radians" };
    bool compact = false;
    if (e && target) {
        assert(graph_set_pin_binding(asset->graph, target, 0, 0, &binding) == 0);
        compact = ed_pin_rows(e, asset->graph, target, ED_PIN_INPUT, 0) == 1;
    }
    test_check(compact, "scalar bindings share the target label row");
    if (e) {
        editor_free(e);
    } else {
        behavior_asset_free(asset);
    }
}

static void check_bound_vector_layout()
{
    behavior_asset *asset = behavior_asset_new("Bound vector layout");
    editor *e = editor_new(asset);
    node *target = asset ? graph_add_node(asset->graph, &g_editor_vector_sink_desc)
                         : NULL;
    pin_binding binding = { .kind = PIN_BINDING_COMPONENT,
                            .component_key = "GAME.GameVelocity" };
    bool compact = false;
    if (e && target) {
        assert(graph_set_pin_binding(asset->graph, target, 0, 0, &binding) == 0);
        compact = ed_pin_rows(e, asset->graph, target, ED_PIN_INPUT, 0) == 1;
    }
    test_check(compact, "vector component bindings share the target label row");
    if (e) {
        editor_free(e);
    } else {
        behavior_asset_free(asset);
    }
}

static void check_component_struct_layout()
{
    behavior_asset *asset = behavior_asset_new("Component struct layout");
    editor *e = editor_new(asset);
    const node_desc *desc = registry_node_by_name("SetEntity_GameTransform");
    node *target = asset && desc ? graph_add_node(asset->graph, desc) : NULL;
    bool value_single_row = false;
    bool value_control_aligned = false;
    if (e && target) {
        pin_binding binding = { .kind = PIN_BINDING_COMPONENT,
                                .component_key = "GAME.GameTransform" };
        assert(graph_set_pin_binding(asset->graph, target, 0, 2, &binding) == 0);
        value_single_row = ed_pin_rows(e, asset->graph, target, ED_PIN_INPUT, 2) == 1;

        ImGuiIO *io = ImGui_GetIO();
        io->DisplaySize = (ImVec2){ 800, 600 };
        io->DeltaTime = 1.0f / 60.0f;
        ImFontAtlas_Build(io->Fonts);
        e->icon_font = ed_find_font("Chibi Body");
        e->math_font = ed_find_font("Chibi Math");
        ImGui_NewFrame();
        ImGui_SetNextWindowPos((ImVec2){ 0, 0 }, ImGuiCond_Always);
        ImGui_SetNextWindowSize((ImVec2){ 800, 600 }, ImGuiCond_Always);
        ImGui_Begin("Component struct layout", NULL, ImGuiWindowFlags_NoSavedSettings);
        e->canvas_min = ImGui_GetCursorScreenPos();
        e->canvas_size = ImGui_GetContentRegionAvail();
        e->pan = (ImVec2){ 0, 0 };
        e->zoom = 1.0f;
        ed_build_layout(e);
        ed_draw_canvas(e);
        ImVec2 control_min = ImGui_GetItemRectMin();
        ImVec2 control_max = ImGui_GetItemRectMax();
        const ed_pin_view *value_pin = NULL;
        for (int i = 0; i < acount(e->pins); i++) {
            if (e->pins[i].n == target && e->pins[i].kind == ED_PIN_INPUT &&
                e->pins[i].index == 2) {
                value_pin = &e->pins[i];
                break;
            }
        }
        float control_center = (control_min.y + control_max.y) * 0.5f;
        value_control_aligned = value_pin &&
                                control_center - value_pin->pos.y > -0.1f &&
                                control_center - value_pin->pos.y < 0.1f;
        ImGui_End();
        ImGui_EndFrame();
    }
    test_check(value_single_row, "component setter values use one graph row");
    test_check(value_control_aligned, "component setter value controls share their pin row");
    if (e) {
        editor_free(e);
    } else {
        behavior_asset_free(asset);
    }
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
                                  "Behavior Expression" };
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
    int comment_count = acount(asset->graph->comment_boxes);
    search_popup_insert(e, "Comment Box");
    test_check(acount(asset->graph->comment_boxes) == comment_count + 1, "typing Comment Box in the context popup inserts a comment box from the standard results");
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
    behavior_asset *fields = behavior_asset_new("ScalarFields");
    fields->domain = BEHAVIOR_DOMAIN_LIBRARY;
    graph *disc = behavior_asset_add_function(fields, "Disc", true);
    node *disc_return = graph_node_by_id(disc, disc->return_id);
    graph_add_pin(disc, disc_return, 0, &(pin_spec){ .name = "Value", .type = PIN_F32 });
    graph_add_node(disc, registry_node_by_name("Field Circle"));
    asset->domain = BEHAVIOR_DOMAIN_FIELD;
    const behavior_asset *field_assets[] = { fields };
    editor_set_asset_catalog(e, field_assets, 1);
    ed_search_match *field_matches = ed_search_matches(e, "ScalarFields / Disc");
    node *field_call = acount(field_matches) ? ed_spawn_match(e, &field_matches[0])
                                             : NULL;
    test_check(
        field_call && field_call->desc == &NODE_CallPureFunction &&
            field_call->reference_asset &&
            !strcmp(field_call->reference_asset, "ScalarFields"),
        "field search inserts pure scalar library functions"
    );
    afree(field_matches);
    field_matches = ed_search_matches(e, "Print String");
    test_check(!acount(field_matches), "field search excludes host effects");
    afree(field_matches);
    behavior_asset_free(fields);
    editor_free(e);
    behavior_asset_free(library);
    behavior_asset_free(event_definition);
}

static void check_field_preview()
{
    behavior_asset *asset = behavior_asset_new("Field preview");
    asset->domain = BEHAVIOR_DOMAIN_FIELD;
    node *x = graph_add_node(asset->graph, registry_node_by_name("Field X"));
    node *y = graph_add_node(asset->graph, registry_node_by_name("Field Y"));
    node *disc = graph_add_node(asset->graph, registry_node_by_name("Field Circle"));
    node *out = graph_add_node(asset->graph, registry_node_by_name("Field Output"));
    graph_set_pin_default(asset->graph, disc, 0, 2, &(pin_value){ .type = PIN_F32, .f = 40 });
    graph_connect_data(asset->graph, x, 0, disc, 0);
    graph_connect_data(asset->graph, y, 0, disc, 1);
    graph_connect_data(asset->graph, disc, 0, out, 0);
    editor *e = editor_new(asset);
    ImGui_NewFrame();
    ImGui_Begin("Field details", NULL, 0);
    ed_draw_field_details(e);
    ImGui_End();
    ImGui_Render();
    test_check(
        e->field_preview_valid && e->field_values[32 * 64 + 32] < 0 &&
            e->field_values[0] > 0,
        "field Details bakes and draws the authored contour"
    );
    graph_set_pin_default(asset->graph, disc, 0, 2, &(pin_value){ .type = PIN_F32, .f = 80 });
    e->edit_revision++;
    ImGui_NewFrame();
    ImGui_Begin("Field details", NULL, 0);
    ed_draw_field_details(e);
    ImGui_End();
    ImGui_Render();
    test_check(e->field_preview_valid && e->field_values[32 * 64 + 32] < -70, "field Details refreshes after an authored edit");
    editor_free(e);
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

/* Behavior graphs offer Get Dash as a Get Component preset. */
static bool get_dash(const ed_search_match *match)
{
    return match->desc == &NODE_GetComponent && match->component &&
           !strcmp(match->component, "GAME.GameDash") && !match->field;
}

static void check_typed_wire_search()
{
    behavior_asset *asset = behavior_asset_new("Typed wire search");
    editor *e = editor_new(asset);
    node *target = asset ? graph_add_node(asset->graph, &g_editor_float_sink_desc)
                         : NULL;
    const node_desc *dash = registry_node_by_name("Get_GameDash");
    bool behavior_match = false;
    bool system_match = false;
    if (e && target && dash) {
        e->spawn_link_pending = true;
        e->spawn_link_pin = (ed_pin_view){ target, ED_PIN_INPUT, 0, { 0, 0 } };
        ed_search_match *matches = ed_search_matches(e, "Get Dash");
        for (int i = 0; i < acount(matches); i++) {
            behavior_match |= get_dash(&matches[i]);
        }
        afree(matches);
        editor_set_ui_context(e, EDITOR_UI_CONTEXT_SYSTEM);
        matches = ed_search_matches(e, "Get Dash");
        for (int i = 0; i < acount(matches); i++) {
            system_match |= matches[i].desc == dash;
        }
        afree(matches);
    }
    test_check(behavior_match, "a numeric input suggests Get Dash in the behavior graph");
    test_check(system_match, "a numeric input suggests Get Dash in the system graph");

    bool inline_field = false;
    if (e && target && dash) {
        e->spawn_link_pending = false;
        editor_set_ui_context(e, EDITOR_UI_CONTEXT_BEHAVIOR);
        ed_search_match *matches = ed_search_matches(e, "Get Dash");
        for (int i = 0; i < acount(matches); i++) {
            if (!get_dash(&matches[i])) {
                continue;
            }
            for (int p = 0; p < ed_search_match_output_count(e, &matches[i]); p++) {
                ed_search_output output;
                if (ed_search_match_output(e, &matches[i], p, &output) &&
                    output.node_pin == 1 && output.pin.type == PIN_F32 &&
                    output.pin.name && !strcmp(output.pin.name, "speed")) {
                    inline_field = true;
                }
            }
        }
        afree(matches);
    }
    test_check(inline_field, "inline source search exposes Get Dash numeric fields");

    bool connected = false;
    if (e && target && dash) {
        asset->graph->dataflow = false;
        e->spawn_link_pending = true;
        e->spawn_link_pin = (ed_pin_view){ target, ED_PIN_INPUT, 0, { 0, 0 } };
        search_popup_insert(e, "Get Dash");
        const pin_binding *binding = graph_pin_binding(asset->graph, target, 0, 0);
        connected = binding && binding->kind == PIN_BINDING_COMPONENT_FIELD &&
                    binding->component_key &&
                    !strcmp(binding->component_key, "GAME.GameDash") &&
                    binding->field_name && !strcmp(binding->field_name, "speed");
    }
    test_check(connected, "selecting Get Dash from a numeric input binds a compatible field output");

    behavior_asset *direct_asset = behavior_asset_new("Direct wire replaces inline source");
    editor *direct_editor = editor_new(direct_asset);
    graph *direct = direct_asset->graph;
    node *old_source = graph_add_node(direct, registry_node_by_name("Real"));
    node *new_source = graph_add_node(direct, registry_node_by_name("Real"));
    node *direct_target = graph_add_node(direct, &g_editor_float_sink_desc);
    assert(direct_editor && old_source && new_source && direct_target);
    assert(graph_connect(direct, old_source, 0, direct_target, 0) == 0);
    uint64_t old_source_id = old_source->id;
    bool replaced = graph_node_is_inline(direct, old_source) &&
                    direct_pin_drag(
                        direct_editor,
                        (ed_pin_view){ new_source, ED_PIN_OUTPUT, 0, { 80, 100 } },
                        (ed_pin_view){ direct_target, ED_PIN_INPUT, 0, { 180, 100 } }
                    );
    node *linked_source = NULL;
    int linked_pin = -1;
    test_check(
        replaced && !graph_node_by_id(direct, old_source_id) &&
            graph_inline_source(direct, direct_target, 0, &linked_source, &linked_pin) &&
            linked_source == new_source,
        "a wire dropped on an input replaces its inline source"
    );
    editor_free(direct_editor);

    behavior_asset *system_asset = behavior_asset_new("System typed wire search");
    editor *system_editor = editor_new(system_asset);
    node *system_target = system_asset
                            ? graph_add_node(system_asset->graph, &g_editor_float_sink_desc)
                            : NULL;
    bool system_connected = false;
    if (system_editor && system_target && dash) {
        editor_set_ui_context(system_editor, EDITOR_UI_CONTEXT_SYSTEM);
        system_editor->spawn_link_pending = true;
        system_editor->spawn_link_pin = (ed_pin_view){ system_target,
                                                       ED_PIN_INPUT,
                                                       0,
                                                       { 0, 0 } };
        search_popup_insert(system_editor, "Get Dash");
        node *source = NULL;
        int source_pin = -1;
        system_connected = graph_input_source(system_asset->graph, system_target, 0, &source, &source_pin) &&
                           source && source->desc == dash && source_pin >= 0 &&
                           source_pin < source->output_count &&
                           source->output_pins[source_pin].type == PIN_F32 &&
                           source->output_pins[source_pin].name &&
                           !strcmp(source->output_pins[source_pin].name, "speed");
    }
    test_check(system_connected, "system popup autowires a compatible Get Dash field output");
    editor_free(system_editor);
    editor_free(e);
}

static void check_binding_target_stability()
{
    behavior_asset *asset = behavior_asset_new("Binding targets");
    editor *e = editor_new(asset);
    node *bound = graph_add_node(asset->graph, &g_editor_float_sink_desc);
    node *target = graph_add_node(asset->graph, &g_editor_float_sink_desc);
    pin_binding binding = { .kind = PIN_BINDING_DELTA_TIME };
    assert(graph_set_pin_binding(asset->graph, bound, 0, 0, &binding) == 0);
    ed_open_binding_popup(e, target, 0);
    ImGui_NewFrame();
    ImGui_Begin("Inspector regression", NULL, 0);
    ed_draw_details_pin(e, bound, 0, 0);
    ImGui_End();
    ImGui_EndFrame();
    test_check(e->binding_node_id == target->id && e->binding_pin == 0 && e->binding_open_requested, "drawing a bound Inspector input preserves another input's pending binding target");
    e->binding_open_requested = false;
    editor_set_event_catalog(e, APP_EVENT_CATALOG, sizeof APP_EVENT_CATALOG / sizeof *APP_EVENT_CATALOG);
    search_popup_insert(e, "On Begin");
    node *begin = asset->graph->nodes[acount(asset->graph->nodes) - 1];
    test_check(begin->reference_name && !strcmp(begin->reference_name, "Begin"), "the application event catalog exposes behavior initialization through node search");
    editor_free(e);
}

static void check_event_payload_repair()
{
    behavior_asset *asset = behavior_asset_new("Event repair");
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, event, "Tick") == 0);
    pin_spec payload = { "Bound", PIN_F32, "float", PIN_DATA };
    assert(graph_add_pin(g, event, 1, &payload) == 1);
    payload.name = "Unused";
    assert(graph_add_pin(g, event, 1, &payload) == 2);
    node *sink = graph_add_node(g, registry_node_by_name("Reroute Data"));
    assert(graph_set_pin_type(g, sink, 0, 0, PIN_F32, "float") == 0);
    uint64_t event_id = event->id;
    uint64_t payload_id = event->output_pins[1].id;
    assert(
        graph_set_pin_binding(
            g,
            sink,
            0,
            0,
            &(pin_binding){
                .kind = PIN_BINDING_EVENT_OUTPUT,
                .event_node_id = event_id,
                .event_pin_id = payload_id,
            }
        ) == 0
    );
    editor *e = editor_new(asset);
    editor_tick(e, 0);
    test_check(ed_event_unused_payload_count(e, event) == 1, "event repair preserves payloads used by direct input bindings");
    test_check(
        ed_repair_event_payloads(e, event) == 1 && event->output_count == 2 &&
            event->output_pins[1].id == payload_id,
        "event repair removes only the unused payload"
    );
    editor_tick(e, 1);
    editor_undo(e);
    event = graph_node_by_id(e->active_graph, event_id);
    test_check(event && event->output_count == 3, "event payload repair can be undone");
    editor_redo(e);
    event = graph_node_by_id(e->active_graph, event_id);
    test_check(
        event && event->output_count == 2 && event->output_pins[1].id == payload_id &&
            ed_event_unused_payload_count(e, event) == 0,
        "redo preserves the directly bound event payload"
    );
    editor_free(e);
}

/* Browsing the catalog with no query: entries by kind. */
static void component_catalog_counts(const editor *e, const char *label)
{
    ed_search_match *matches = ed_search_matches(e, "");
    int access = 0, fields = 0, structural = 0, make_break = 0;
    for (int i = 0; i < acount(matches); i++) {
        const node_desc *d = matches[i].desc;
        fields += d->component_kind == NODE_COMPONENT_SET_FIELD;
        access += d->component_kind != NODE_COMPONENT_NONE &&
                  d->component_kind != NODE_COMPONENT_SET_FIELD;
        structural += !strncmp(d->name, "Add_", 4) || !strncmp(d->name, "Remove_", 7);
        make_break += d->pure_kind == NODE_PURE_MAKE || d->pure_kind == NODE_PURE_BREAK;
    }
    printf(
        "node catalog %s: %d entries; %d component access, %d field setters, "
        "%d add/remove, %d make/break\n",
        label,
        acount(matches),
        access,
        fields,
        structural,
        make_break
    );
    afree(matches);
}

static node *spawn_first(editor *e, const char *query, const char *name)
{
    ed_search_match *matches = ed_search_matches(e, query);
    bool found = acount(matches) && !strcmp(matches[0].name, name);
    node *n = found ? ed_spawn_match(e, &matches[0]) : NULL;
    afree(matches);
    return n;
}

/* The catalog lists Get, Set and Has Component instead of generated
 * per-component nodes; naming a component spawns a configured node. */
static void check_component_search()
{
    behavior_asset *asset = behavior_asset_new("Component search");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    editor *e = editor_new(asset);
    component_catalog_counts(e, "behavior");
    ed_search_match *matches = ed_search_matches(e, "");
    int component_nodes = 0, generated = 0;
    for (int i = 0; i < acount(matches); i++) {
        component_nodes += component_node_kind_of(matches[i].desc) &&
                           !matches[i].component;
        generated += matches[i].desc->component_kind != NODE_COMPONENT_NONE;
    }
    afree(matches);
    test_check(component_nodes == 3 && !generated, "browsing lists three component nodes and no generated access nodes");
    node *get = spawn_first(e, "Get Component", "Get Component");
    test_check(
        get && get->desc == &NODE_GetComponent && !component_node_component(get),
        "the generic node spawns without a component"
    );
    node *velocity = spawn_first(e, "Set Velocity", "Set Velocity");
    test_check(
        velocity && velocity->desc == &NODE_SetComponent &&
            !strcmp(component_node_key(velocity), "GAME.GameVelocity") &&
            !component_node_split(velocity) &&
            !strcmp(velocity->input_pins[3].type_name, "GameVelocity") &&
            !strcmp(velocity->output_pins[1].type_name, "GameVelocity"),
        "Set Velocity spawns Set Component writing Velocity"
    );
    node *x = spawn_first(e, "Set Velocity X", "Set Velocity X");
    test_check(
        x && component_node_split(x) && x->input_count == 4 &&
            !strcmp(x->input_pins[3].name, "x") && x->input_pins[3].type == PIN_F32,
        "Set Velocity X spawns Set Component showing only X"
    );
    node *health = spawn_first(e, "has health", "Has Health");
    test_check(
        health && health->desc == &NODE_HasComponent &&
            !strcmp(component_node_key(health), "GAME.GameHealth"),
        "Has Health spawns Has Component for Health"
    );
    node *transform = spawn_first(e, "get transform", "Get Transform");
    test_check(
        transform && !strcmp(transform->output_pins[0].type_name, "GameTransform") &&
            (transform->output_pins[0].flags & PIN_FLAG_REFERENCE),
        "Get Transform spawns Get Component reading Transform"
    );
    char title[256];
    test_check(
        transform &&
            !strcmp(ed_node_title(e, transform, title, sizeof title), "Get Transform") &&
            x && !strcmp(ed_node_title(e, x, title, sizeof title), "Set Velocity") &&
            get && !strcmp(ed_node_title(e, get, title, sizeof title), "Get Component"),
        "component nodes are titled by access and component"
    );
    editor_free(e);
}

void run_editor_command_checks(void)
{
    ImGuiContext *context = ImGui_CreateContext(NULL);
    editor_ui_initialize(false);
    check_linked_preview();
    check_search_catalog();
    check_component_search();
    check_library_search_snapshot();
    check_field_preview();
    check_typed_wire_search();
    check_binding_target_stability();
    check_event_payload_repair();
    check_insert_on_wire();
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
    check_inline_node_layout();
    check_delete_shared_source();
    check_value_layout();
    check_context_layout();
    check_bound_scalar_layout();
    check_bound_vector_layout();
    check_component_struct_layout();
    ImGui_DestroyContext(context);
}
