/* End to end on arena2d's Neon Swarm Sandbox (wave 3, workstream X): the HUD
 * document shows what the HudDisplay behavior sets, and the HUD's Repair
 * button and H key reach the behavior. Scenarios run through the controller,
 * as the MCP runs them, and the document renders headless. */
#include "automation/controller.h"
#include "render/ui.h"
#include "runtime/behaviors/behavior.h"
#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "test/suite_internal.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROOT "projects/arena2d"
/* The Player flies right from the first frame, and at frame 5 a Material
   Impact of 15 takes its hull from 20 to 5. */
#define HIT                                                                    \
    "{\"frame\":0,\"input\":{\"movement\":{\"x\":1,\"y\":0}}},"                \
    "{\"frame\":5,\"input\":{\"events\":[{\"name\":\"Material Impact\","       \
    "\"target\":\"Player\",\"values\":[{\"type\":\"float\",\"value\":15},"     \
    "{\"type\":\"int\",\"value\":0}]}]}}"

/* Sends an operation's fields to the controller for arena2d. */
static yyjson_doc *ask(const char *operation, const char *fields)
{
    char request[2048];
    snprintf(
        request,
        sizeof request,
        "{\"contract\":1,\"operation\":\"%s\","
        "\"project_root\":\"" ROOT "\",%s}",
        operation,
        fields
    );
    int exit_code = 0;
    char *response = chibi_automation_handle(request, ".", &exit_code);
    yyjson_doc *doc = yyjson_read(response, strlen(response), 0);
    free(response);
    assert(doc && yyjson_get_bool(yyjson_obj_get(yyjson_doc_get_root(doc), "ok")));
    return doc;
}

static yyjson_val *result_of(yyjson_doc *doc)
{
    return yyjson_obj_get(yyjson_doc_get_root(doc), "result");
}

/* Sandbox after `frames` frames of `timeline`. */
static yyjson_doc *snapshot(int frames, const char *timeline)
{
    char fields[1024];
    snprintf(
        fields,
        sizeof fields,
        "\"kind\":\"scene\",\"name\":\"Sandbox\",\"frames\":%d,"
        "\"entity_limit\":64,\"fields\":[\"GameHealth.current\"],"
        "\"input_timeline\":[%s]",
        frames,
        timeline
    );
    return ask("runtime_snapshot", fields);
}

/* The Player's hull in a snapshot. */
static int hull(yyjson_val *result)
{
    yyjson_val *entity, *component, *field;
    size_t i, max, j, jmax, k, kmax;
    yyjson_arr_foreach(yyjson_obj_get(result, "entities"), i, max, entity)
    {
        if (!yyjson_equals_str(yyjson_obj_get(entity, "name"), "Player")) {
            continue;
        }
        yyjson_arr_foreach(yyjson_obj_get(entity, "components"), j, jmax, component)
        {
            yyjson_arr_foreach(yyjson_obj_get(component, "fields"), k, kmax, field)
            {
                if (yyjson_equals_str(yyjson_obj_get(field, "name"), "current")) {
                    return yyjson_get_int(yyjson_obj_get(field, "value"));
                }
            }
        }
    }
    return -1;
}

/* What the snapshot's behaviors set under `selector`, or NULL. */
static yyjson_val *ui_record(yyjson_val *result, const char *selector)
{
    yyjson_val *record;
    size_t i, max;
    yyjson_arr_foreach(yyjson_obj_get(result, "ui"), i, max, record)
    {
        if (yyjson_equals_str(yyjson_obj_get(record, "element"), selector)) {
            return record;
        }
    }
    return NULL;
}

/* Sets in `model`, for `entity`, what the snapshot's behaviors set, as the
 * game's frame would. */
static void apply_records(game_ui *model, uint64_t entity, yyjson_val *result)
{
    yyjson_val *record, *name, *on;
    size_t i, max, j, jmax;
    yyjson_arr_foreach(yyjson_obj_get(result, "ui"), i, max, record)
    {
        const char *selector = yyjson_get_str(yyjson_obj_get(record, "element"));
        yyjson_val *text = yyjson_obj_get(record, "text");
        yyjson_val *value = yyjson_obj_get(record, "value");
        if (text) {
            game_ui_set_text(model, entity, selector, yyjson_get_str(text));
        }
        if (value) {
            game_ui_set_value(model, entity, selector, (float)yyjson_get_num(value));
        }
        yyjson_val *classes = yyjson_obj_get(record, "classes");
        if (!classes) {
            continue;
        }
        yyjson_obj_foreach(classes, j, jmax, name, on)
        {
            game_ui_set_class(model, entity, selector, yyjson_get_str(name), yyjson_get_bool(on));
        }
    }
}

static void press(render_ui *ui, rml_input_type type, float x, float y)
{
    render_ui_input(ui, &(rml_input){ .type = type, .x = x, .y = y });
}

/* Clicks the center of `element` as the player's pointer would. */
static void click(render_ui *ui, rml_element *element)
{
    rml_vec2 at = rml_element_absolute_offset(element, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(element, RML_BOX_BORDER);
    float x = at.x + size.x / 2, y = at.y + size.y / 2;
    press(ui, RML_INPUT_POINTER_MOVE, x, y);
    press(ui, RML_INPUT_POINTER_DOWN, x, y);
    press(ui, RML_INPUT_POINTER_UP, x, y);
}

static bool text_is(rml_element *root, const char *selector, const char *expected)
{
    char text[64] = "";
    rml_element *element = rml_element_query_selector(root, selector);
    return element && rml_element_inner_rml(element, text, sizeof text) &&
           !strcmp(text, expected);
}

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    ecs_entity hud;
    render_ui *ui;
    ui_world w;
} hud_view;

/* The HUD document alone, rendered at 720p as the Sandbox shows it. */
static hud_view hud_open()
{
    hud_view v = { .world = ecs_world_new() };
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(v.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    v.live = game_live_world_new(v.world);
    v.hud = ecs_world_create_entity(v.world);
    assert(GAME_GameUi_add(v.world, v.hud, 0) == ECS_OK);
    assert(ecs_world_apply_commands(v.world) == ECS_OK);
    *GAME_GameUi_get(v.world, v.hud) = (GameUi){ .document = "HUD" };
    v.ui = render_ui_new(ROOT "/content", NULL);
    v.w = (ui_world){ .ecs = v.world,
                      .model = game_behavior_ui(v.live->behaviors),
                      .entities = v.live->entities };
    return v;
}

static void hud_frame(hud_view *v)
{
    render_ui_update(v->ui, &v->w, 1280, 720, 1.0 / 60);
}

static rml_element *hud_document(hud_view *v)
{
    return rml_context_document(render_ui_context(v->ui), 0);
}

static void hud_close(hud_view *v)
{
    render_ui_free(v->ui);
    game_live_world_free(v->live);
    ecs_world_free(v->world);
}

/* The event the HUD's #repair element sends, as inspectUi lists it. */
static void repair_event(char *event, size_t size)
{
    yyjson_doc *doc = ask("inspect_ui", "\"kind\":\"ui\",\"name\":\"HUD\"");
    yyjson_val *item;
    size_t i, max;
    event[0] = '\0';
    yyjson_arr_foreach(yyjson_obj_get(result_of(doc), "events"), i, max, item)
    {
        if (yyjson_equals_str(yyjson_obj_get(item, "element"), "#repair")) {
            snprintf(event, size, "%s", yyjson_get_str(yyjson_obj_get(item, "name")));
        }
    }
    yyjson_doc_free(doc);
}

/* previewScene prepares the Sandbox, though its project's shader library is
   among the assets every scenario registers. */
static void sandbox_previews()
{
    chibi_preview_run *run = NULL;
    bool perspective = false;
    char error[256] = "";
    int status = chibi_automation_preview(
        ROOT,
        "{\"kind\":\"scene\",\"name\":\"Sandbox\",\"frames\":1}",
        &run,
        &perspective,
        error,
        sizeof error
    );
    if (status) {
        printf("  %s\n", error);
    }
    test_check(!status && run, "arena2d: previewScene prepares the Sandbox, shader library and all");
    chibi_preview_free(run);
}

void test_arena_hud()
{
    sandbox_previews();
    yyjson_doc *hit = snapshot(12, HIT);
    yyjson_val *damaged = result_of(hit);
    yyjson_val *bar = ui_record(damaged, "#hull .bar");
    yyjson_val *critical = yyjson_obj_get(ui_record(damaged, "#hull"), "classes");
    double speed = yyjson_get_num(yyjson_obj_get(ui_record(damaged, "#speed"), "value"));
    test_check(
        hull(damaged) == 5 && bar && speed > 100 &&
            yyjson_get_num(yyjson_obj_get(bar, "value")) == 0.25 &&
            yyjson_get_bool(yyjson_obj_get(critical, "critical")) &&
            yyjson_equals_str(yyjson_obj_get(ui_record(damaged, "#hull .readout"), "text"), "5 / 20"),
        "arena2d: HudDisplay sets the flying, damaged Player's hull and speed "
        "on the Sandbox HUD"
    );

    hud_view v = hud_open();
    apply_records(v.w.model, ecs_entity_to_u64(v.hud), damaged);
    hud_frame(&v);
    hud_frame(&v);
    rml_element *doc = hud_document(&v);
    char value[16] = "";
    rml_element *meter = rml_element_query_selector(doc, "#hull .bar");
    rml_element_attribute(meter, "value", value, sizeof value);
    test_check(
        !render_ui_error(v.ui)[0] && text_is(doc, "#hull .readout", "5 / 20") &&
            !strcmp(value, "0.25") &&
            rml_element_is_class_set(rml_element_query_selector(doc, "#hull"), "critical"),
        "arena2d: the HUD document shows the hull the behavior set"
    );
    int before = game_entities_pending_events(v.live->entities);
    click(v.ui, rml_element_query_selector(doc, "#repair"));
    hud_frame(&v);
    bool clicked = game_entities_pending_events(v.live->entities) == before + 1;
    render_ui_input(v.ui, &(rml_input){ .type = RML_INPUT_KEY_PRESS, .key = RML_INPUT_KEY_H });
    hud_frame(&v);
    bool keyed = game_entities_pending_events(v.live->entities) == before + 2;
    test_check(clicked && keyed, "arena2d: the HUD's Repair button and H key send its event");
    hud_close(&v);
    yyjson_doc_free(hit);

    char event[64], timeline[1024];
    repair_event(event, sizeof event);
    snprintf(
        timeline,
        sizeof timeline,
        HIT ",{\"frame\":12,\"input\":{\"ui\":[{\"target\":\"HUD\","
            "\"element\":\"#repair\",\"event\":\"%s\"}]}}",
        event
    );
    yyjson_doc *repaired_doc = snapshot(20, timeline);
    yyjson_val *repaired = result_of(repaired_doc);
    critical = yyjson_obj_get(ui_record(repaired, "#hull"), "classes");
    test_check(
        !strcmp(event, "Repair") && hull(repaired) == 20 &&
            !yyjson_get_bool(yyjson_obj_get(critical, "critical")) &&
            yyjson_equals_str(yyjson_obj_get(ui_record(repaired, "#hull .readout"), "text"), "20 / 20"),
        "arena2d: the HUD's Repair event reaches HudDisplay, which restores "
        "the hull"
    );
    yyjson_doc_free(repaired_doc);
}
