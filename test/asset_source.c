#include "assets/asset_source.h"

#include "graph/document/events.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "support/array.h"
#include "support/asset_text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool has_completion(const asset_source_completions *choices, const char *label)
{
    for (int i = 0; i < acount(choices->items); i++) {
        if (!strcmp(choices->items[i].label, label)) {
            return true;
        }
    }
    return false;
}

static asset_source_completions complete_draft(const char *text)
{
    asset_source *source = asset_source_parse(text);
    asset_source_completions result;
    asset_source_complete(source, (int)strlen(text), "Behavior.code", NULL, 0, &result);
    asset_source_free(source);
    return result;
}

static const char *source_event_name(const graph_host_event_desc *event)
{
    return !strcmp(event->name, "Contact Begin") ? "ContactBegin" : event->name;
}

static const char *source_payload_name(const char *name)
{
    if (!strcmp(name, "DeltaSeconds")) {
        return "dt";
    }
    if (!strcmp(name, "Direction")) {
        return "direction";
    }
    if (!strcmp(name, "Other")) {
        return "other";
    }
    if (!strcmp(name, "Point")) {
        return "point";
    }
    return name;
}

static void test_structured_completions()
{
    asset_source_completions choices = complete_draft("behavior Test { state ");
    assert(has_completion(&choices, "entity") && has_completion(&choices, "string"));
    assert(!has_completion(&choices, "handle") && !has_completion(&choices, "map"));
    asset_source_completions_free(&choices);
    choices = complete_draft("behavior Test { state bool enabled = ");
    assert(has_completion(&choices, "true") && has_completion(&choices, "false"));
    assert(!has_completion(&choices, "on"));
    asset_source_completions_free(&choices);
    choices = complete_draft("behavior Test { on ");
    for (int i = 0; i < GRAPH_HOST_EVENT_COUNT; i++) {
        assert(has_completion(&choices, source_event_name(&GRAPH_HOST_EVENTS[i])));
    }
    assert(!has_completion(&choices, "Contact Begin"));
    asset_source_completions_free(&choices);
    for (int i = 0; i < GRAPH_HOST_EVENT_COUNT; i++) {
        const graph_host_event_desc *event = &GRAPH_HOST_EVENTS[i];
        char draft[512];
        snprintf(draft, sizeof draft, "behavior Test { on %s() { event.", source_event_name(event));
        choices = complete_draft(draft);
        assert(choices.start == (int)strlen(draft));
        assert(acount(choices.items) == event->payload_count);
        for (int p = 0; p < event->payload_count; p++) {
            const pin_spec *pin = &event->payload[p];
            assert(has_completion(&choices, source_payload_name(pin->name)));
        }
        asset_source_completions_free(&choices);
    }
    const type_desc *vector = registry_type_by_name("CF_V2");
    if (vector) {
        choices = complete_draft("behavior Test { on Input() { event.direction.");
        for (int i = 0; i < vector->field_count; i++) {
            assert(has_completion(&choices, vector->fields[i].name));
        }
        asset_source_completions_free(&choices);
    }
    choices = complete_draft("behavior Test { state set<int> seen = []; on Tick() { seen.");
    assert(has_completion(&choices, "add") && has_completion(&choices, "remove"));
    assert(!has_completion(&choices, "contains"));
    asset_source_completions_free(&choices);
    choices = complete_draft("behavior Test { state set<int> seen = []; on Tick() { if (seen.");
    assert(has_completion(&choices, "contains"));
    assert(!has_completion(&choices, "add") && !has_completion(&choices, "remove"));
    asset_source_completions_free(&choices);
    choices = complete_draft("behavior Test { state set<int> seen = []; state int count = 0; on Tick() { count = seen.");
    assert(!acount(choices.items));
    asset_source_completions_free(&choices);
    choices = complete_draft("behavior Test { state int[] values = []; state int count = 0; on Tick() { for (item in ");
    assert(has_completion(&choices, "values") && !has_completion(&choices, "count"));
    asset_source_completions_free(&choices);
    choices = complete_draft("behavior Test { on Tick() { ");
    static const char *const unsupported[] = { "while",    "break",
                                               "continue", "return",
                                               "map",      "handle",
                                               "nodes",    "components" };
    for (size_t i = 0; i < sizeof unsupported / sizeof *unsupported; i++) {
        assert(!has_completion(&choices, unsupported[i]));
    }
    bool destroy = false;
    for (int i = 0; i < acount(choices.items); i++) {
        assert(choices.items[i].label[0] != '"');
        assert(!strchr(choices.items[i].label, ' '));
        assert(!choices.items[i].insert || choices.items[i].insert[0] != '"');
        if (!strcmp(choices.items[i].label, "destroy")) {
            destroy = choices.items[i].insert &&
                      !strcmp(choices.items[i].insert, "destroy(self);");
        }
    }
    assert(destroy);
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *node = registry_node(i);
        int binding;
        const char *spelling = behavior_expression_spelling(node, &binding);
        if (!spelling || binding || behavior_expression_arity(node) < 0) {
            continue;
        }
        const char *alias = behavior_expression_alias(node, 0);
        if (alias) {
            assert(has_completion(&choices, alias));
        }
    }
    asset_source_completions_free(&choices);
    choices = complete_draft("behavior Test { on Tick() { if (true) {} ");
    assert(has_completion(&choices, "else"));
    asset_source_completions_free(&choices);
    choices = complete_draft("behavior Test { state int[] values = []; on Tick() { for (item in values) {} ");
    assert(!has_completion(&choices, "else"));
    asset_source_completions_free(&choices);
    choices = complete_draft("behavior Test { on Tick() { // incomplete comment");
    assert(!acount(choices.items));
    asset_source_completions_free(&choices);
    choices = complete_draft("behavior Test { on Tick() {} ");
    assert(has_completion(&choices, "state") && has_completion(&choices, "on"));
    assert(!has_completion(&choices, "destroy"));
    asset_source_completions_free(&choices);
}

static void test_structured_navigation()
{
    asset_source *source = asset_source_parse(
        "behavior Test { state int points = 0; on Tick() { poi"
    );
    asset_source_completions choices;
    asset_source_complete(source, (int)strlen(source->text), "Test.code", NULL, 0, &choices);
    assert(has_completion(&choices, "points"));
    asset_source_completions_free(&choices);
    asset_source_free(source);
    const char *navigation = "behavior Test { state int points = 0; on Tick() "
                             "{ int local = points; local += 1; } on Other() { "
                             "int local = 2; local += points; } }";
    source = asset_source_parse(navigation);
    int use = (int)(strstr(navigation, "local += 1") - navigation);
    asset_source_span definition;
    assert(asset_source_definition_at(source, use, &definition));
    assert(definition.start == (int)(strstr(navigation, "local = points") - navigation));
    asset_source_span *references = asset_source_references_at(source, use);
    assert(acount(references) == 2);
    afree(references);
    char *hover = asset_source_hover(source, use);
    assert(hover && strstr(hover, "local: int"));
    free(hover);
    asset_source_free(source);
    const char *same_name = "behavior Test { state int Tick = 0; on Tick() { "
                            "Tick += 1; } }";
    source = asset_source_parse(same_name);
    use = (int)(strstr(same_name, "Tick +=") - same_name);
    assert(asset_source_definition_at(source, use, &definition));
    assert(definition.start == (int)(strstr(same_name, "Tick =") - same_name));
    references = asset_source_references_at(source, use);
    assert(acount(references) == 2);
    afree(references);
    asset_source_free(source);
    const char *payload = "behavior Test { on Tick() { event.dt; event.dt; } "
                          "on Input() { event.direction; } }";
    source = asset_source_parse(payload);
    use = (int)(strstr(payload, "event.dt") - payload) + 6;
    hover = asset_source_hover(source, use);
    assert(hover && strstr(hover, "dt: float"));
    free(hover);
    assert(asset_source_definition_at(source, use, &definition));
    assert(definition.start == (int)(strstr(payload, "Tick()") - payload));
    references = asset_source_references_at(source, use);
    assert(acount(references) == 2 && references[0].end - references[0].start == 2);
    afree(references);
    asset_source_free(source);
    for (int i = 0; i < registry_type_count(); i++) {
        const type_desc *type = registry_type(i);
        if (type->field_count < 2) {
            continue;
        }
        char draft[1024];
        snprintf(
            draft,
            sizeof draft,
            "behavior Test { on Tick(%s body) { body.%s; body.%s; body.%s; } }",
            type->name,
            type->fields[0].name,
            type->fields[0].name,
            type->fields[1].name
        );
        source = asset_source_parse(draft);
        use = (int)(strstr(draft, "body.") - draft) + 5;
        hover = asset_source_hover(source, use);
        assert(hover && strstr(hover, type->fields[0].name));
        free(hover);
        assert(asset_source_definition_at(source, use, &definition));
        assert(definition.start == (int)(strstr(draft, type->name) - draft));
        references = asset_source_references_at(source, use);
        assert(acount(references) == 2);
        afree(references);
        asset_source_free(source);
        break;
    }
    asset_source_span *spans = asset_source_highlight(
        "behavior Test { // state fake\n on Tick() { x+=1; } }"
    );
    bool keyword = false, comment = false, operator = false;
    for (int i = 0; i < acount(spans); i++) {
        keyword |= !strcmp(spans[i].cls, "tok-keyword");
        comment |= !strcmp(spans[i].cls, "tok-comment");
        operator |= !strcmp(spans[i].cls, "tok-punctuation");
    }
    assert(keyword && comment && operator);
    afree(spans);
}

static void test_property_completion()
{
    /* Use declared packs so qualified and quoted names exercise the same
     * reflection metadata as loaded projects, including incomplete drafts. */
    for (int p = 0; p < REGISTRY_PACK_COUNT; p++) {
        const registry_pack *pack = REGISTRY_PACKS[p];
        for (int t = 0; t < pack->type_count; t++) {
            const type_desc *type = pack->types[t];
            if (type->field_count < 2) {
                continue;
            }
            char name[256], draft[1024];
            snprintf(name, sizeof name, "%s.%s", pack->name, type->name);
            av_doc *doc = av_doc_new();
            char *literal = av_value_write(av_str(doc, name));
            av_doc_free(doc);
            const char *a = type->fields[0].name, *b = type->fields[1].name;
            snprintf(draft, sizeof draft, "components {\n %s %s=0 ", literal, a);
            asset_source *source = asset_source_parse(draft);
            asset_source_completions choices;
            asset_source_complete(source, (int)strlen(draft), "Test.kdl", NULL, 0, &choices);
            assert(!has_completion(&choices, a) && has_completion(&choices, b));
            asset_source_completions_free(&choices);
            asset_source_free(source);

            /* Properties after the caret are also occupied. A partial key
             * before them remains editable rather than excluding itself. */
            snprintf(draft, sizeof draft, "components {\n %s %s=0  %s=0\n}\n", literal, a, b);
            int at = (int)(strstr(draft, "=0  ") - draft) + 4;
            source = asset_source_parse(draft);
            asset_source_complete(source, at, "Test.kdl", NULL, 0, &choices);
            assert(!has_completion(&choices, a) && !has_completion(&choices, b));
            asset_source_completions_free(&choices);
            at = (int)(strstr(draft, "=0  ") - draft) + 4 + (int)strlen(b);
            asset_source_complete(source, at, "Test.kdl", NULL, 0, &choices);
            assert(has_completion(&choices, b));
            asset_source_completions_free(&choices);
            asset_source_free(source);

            /* Braced fields exclude siblings, while a second component gets
             * a fresh property set even if the previous component used it. */
            snprintf(draft, sizeof draft, "components {\n %s {\n %s 0\n ", literal, a);
            source = asset_source_parse(draft);
            asset_source_complete(source, (int)strlen(draft), "Test.kdl", NULL, 0, &choices);
            assert(!has_completion(&choices, a) && has_completion(&choices, b));
            asset_source_completions_free(&choices);
            asset_source_free(source);
            snprintf(draft, sizeof draft, "components {\n %s %s=0\n %s ", literal, a, literal);
            source = asset_source_parse(draft);
            asset_source_complete(source, (int)strlen(draft), "Test.kdl", NULL, 0, &choices);
            assert(has_completion(&choices, a) && has_completion(&choices, b));
            asset_source_completions_free(&choices);
            asset_source_free(source);
            /* An invalid numeric draft must not hide property completion or
             * forget which key is already present. */
            snprintf(draft, sizeof draft, "components {\n %s %s=0oops ", literal, a);
            source = asset_source_parse(draft);
            asset_source_complete(source, (int)strlen(draft), "Test.kdl", NULL, 0, &choices);
            assert(!has_completion(&choices, a) && has_completion(&choices, b));
            asset_source_completions_free(&choices);
            asset_source_free(source);

            for (int f = 0; f < type->field_count; f++) {
                const type_field *field = &type->fields[f];
                const type_desc *nested = field->type == PIN_STRUCT
                                            ? registry_type_by_name(field->type_name)
                                            : NULL;
                if (!nested || nested->field_count < 2) {
                    continue;
                }
                snprintf(
                    draft,
                    sizeof draft,
                    "components {\n %s {\n %s {\n %s 0\n ",
                    literal,
                    field->name,
                    nested->fields[0].name
                );
                source = asset_source_parse(draft);
                asset_source_complete(source, (int)strlen(draft), "Test.kdl", NULL, 0, &choices);
                assert(!has_completion(&choices, nested->fields[0].name));
                assert(has_completion(&choices, nested->fields[1].name));
                asset_source_completions_free(&choices);
                asset_source_free(source);
                break;
            }
            free(literal);
        }
    }
}

void test_asset_source()
{
    test_property_completion();
    test_structured_completions();
    test_structured_navigation();
    {
        const char *source = "// palette\ncolor 966781183 // water\namount "
                             "0.035000000000000003\n";
        av_doc *values = av_doc_new();
        av_value *root = av_obj(values);
        av_obj_set(values, root, "color", av_color(values, 0x399fe8ff));
        av_obj_set(values, root, "amount", av_float(values, 0.035f));
        char error[256];
        char *saved = asset_source_reconcile(source, root, error, sizeof error);
        assert(saved && !strcmp(saved, "// palette\ncolor 0x399fe8ff // water\namount 0.035\n"));
        char *again = asset_source_reconcile(saved, root, error, sizeof error);
        assert(again && !strcmp(saved, again));
        free(again);
        free(saved);
        av_doc_free(values);
    }
    {
        /* Canonical source has nothing authored to keep: an edit writes the
         * target's canonical text, with new members in the target's order. */
        const char *source = "name \"Removable\"\ncomponents {\n"
                             "    (object)GAME.GameTransform\n}\n";
        const char *canonical = "name \"Removable\"\ncomponents {\n"
                                "    (object)GAME.GameAim\n"
                                "    GAME.GameTransform x=1\n}\n";
        char error[256];
        av_doc *target = av_doc_read(canonical, error, sizeof error);
        assert(target);
        char *saved = asset_source_reconcile(source, av_doc_root(target), error, sizeof error);
        assert(saved && !strcmp(saved, canonical));
        free(saved);
        av_doc_free(target);
    }
    /* A visual field edit preserves hand-authored comments and whitespace,
     * including an inline property and a quoted value with escapes. */
    const char
        *text = "// authored header\nname  \"Player\" // retain this\n"
                "components {\n  Body speed=12 enabled=#true // tuning\n}\n";
    char error[256];
    av_doc *doc = av_doc_read(text, error, sizeof error);
    assert(doc);
    av_value *root = av_doc_root(doc);
    av_value *body = av_obj_get(av_obj_get(root, "components"), "Body");
    av_obj_set(doc, body, "speed", av_sint(doc, 24));
    char *edited = asset_source_reconcile(text, root, error, sizeof error);
    assert(edited);
    assert(!strcmp(
        edited,
        "// authored header\nname  \"Player\" // retain this\n"
        "components {\n  Body speed=24 enabled=#true // tuning\n}\n"
    ));
    free(edited);
    av_obj_add_str(doc, root, "identity", "player");
    edited = asset_source_reconcile(text, root, error, sizeof error);
    assert(edited && strstr(edited, "// authored header") && strstr(edited, "// tuning"));
    av_doc *check = av_doc_read(edited, error, sizeof error);
    assert(check && av_equals(av_doc_root(check), root));
    av_doc_free(check);
    free(edited);
    av_obj_remove_key(body, "enabled");
    av_obj_add_int(doc, body, "mass", 5);
    edited = asset_source_reconcile(text, root, error, sizeof error);
    assert(edited && strstr(edited, "// tuning"));
    check = av_doc_read(edited, error, sizeof error);
    assert(check && av_equals(av_doc_root(check), root));
    av_doc_free(check);
    free(edited);
    av_doc_free(doc);

    /* Changing a list's length replaces its node, including its annotation,
     * while retaining the surrounding and trailing comments exactly. */
    text = "// list header\n(array)items 1 2 // list note\n";
    doc = av_doc_read(text, error, sizeof error);
    assert(doc);
    root = av_doc_root(doc);
    av_arr_append(av_obj_get(root, "items"), av_sint(doc, 3));
    edited = asset_source_reconcile(text, root, error, sizeof error);
    assert(edited && !strcmp(edited, "// list header\n(array)items 1 2 3 // list note\n"));
    free(edited);
    av_doc_free(doc);
    text = "// authored header\nname  \"Player\" // retain this\n"
           "components {\n  Body speed=12 enabled=#true // tuning\n}\n";

    text = "// list header\n(array)items {\n  - value=1 // keep one\n  - "
           "value=2 // keep two\n} // list note\n";
    doc = av_doc_read(text, error, sizeof error);
    root = av_doc_root(doc);
    av_value *objects = av_obj_get(root, "items");
    av_value *third = av_arr_add_obj(doc, objects);
    av_obj_add_int(doc, third, "value", 3);
    edited = asset_source_reconcile(text, root, error, sizeof error);
    assert(
        edited && strstr(edited, "// keep one") &&
        strstr(edited, "// keep two") && strstr(edited, "// list note")
    );
    check = av_doc_read(edited, error, sizeof error);
    assert(check && av_equals(av_doc_root(check), root));
    av_doc_free(check);
    free(edited);
    av_doc_free(doc);

    /* Adding the first nested field promotes an inline map to children while
     * keeping comments outside that node. Shape changes replace annotations. */
    text = "// header\nBody speed=12 // tuning\n";
    doc = av_doc_read(text, error, sizeof error);
    root = av_doc_root(doc);
    body = av_obj_get(root, "Body");
    av_value *position = av_obj_add_obj(doc, body, "position");
    av_obj_add_int(doc, position, "x", 3);
    edited = asset_source_reconcile(text, root, error, sizeof error);
    assert(edited && strstr(edited, "// header") && strstr(edited, "// tuning"));
    check = av_doc_read(edited, error, sizeof error);
    assert(check && av_equals(av_doc_root(check), root));
    av_doc_free(check);
    free(edited);
    av_obj_set(doc, root, "Body", av_bool(doc, true));
    edited = asset_source_reconcile(text, root, error, sizeof error);
    assert(edited && !strcmp(edited, "// header\nBody #true // tuning\n"));
    free(edited);
    av_doc_free(doc);
    text = "name  \"Player\" // retain this\n";
    doc = av_doc_read(text, error, sizeof error);
    root = av_doc_root(doc);
    av_value *list = av_obj_add_arr(doc, root, "name");
    av_arr_add_str(doc, list, "Player");
    edited = asset_source_reconcile(text, root, error, sizeof error);
    assert(edited && strstr(edited, "(array)name") && strstr(edited, "// retain this"));
    check = av_doc_read(edited, error, sizeof error);
    assert(check && av_equals(av_doc_root(check), root));
    av_doc_free(check);
    free(edited);
    av_doc_free(doc);
    text = "// authored header\nname  \"Player\" // retain this\n"
           "components {\n  Body speed=12 enabled=#true // tuning\n}\n";

    /* Pin maps carry meaningful declaration order. Renaming a field replaces
     * its key in place instead of removing it and appending it after siblings. */
    text = "outputs {\n  Entity id=100 type=handle\n  Amount id=103 type=int "
           "// payload\n  Source id=104 type=handle\n}\n";
    doc = av_doc_read(text, error, sizeof error);
    av_doc *target = av_doc_read(
        "outputs {\n Entity id=100 type=handle\n Points id=103 type=int\n "
        "Source id=104 type=handle\n}\n",
        error,
        sizeof error
    );
    assert(doc && target);
    edited = asset_source_reconcile(text, av_doc_root(target), error, sizeof error);
    assert(edited && strstr(edited, "Points id=103 type=int // payload"));
    check = av_doc_read(edited, error, sizeof error);
    av_value *outputs = av_obj_get(av_doc_root(check), "outputs");
    assert(!strcmp(av_get_str(av_obj_key_at(outputs, 1)), "Points"));
    assert(!strcmp(av_get_str(av_obj_key_at(outputs, 2)), "Source"));
    av_doc_free(check);
    av_doc_free(target);
    av_doc_free(doc);
    free(edited);
    text = "// authored header\nname  \"Player\" // retain this\n"
           "components {\n  Body speed=12 enabled=#true // tuning\n}\n";

    /* Incomplete source still highlights the edited token and marks an error. */
    asset_source *source = asset_source_parse("name \"unterminated");
    assert(!source->document && acount(source->diagnostics) > 0);
    assert(acount(source->spans) == 2);
    assert(!strcmp(source->spans[1].cls, "tok-string"));
    asset_source_free(source);

    /* Value locations identify the field independent of formatting. */
    source = asset_source_parse(text);
    const char *name = NULL;
    const av_value *value = asset_source_value_at(source, (int)(strstr(text, "12") - text), &name);
    assert(value && av_get_int(value) == 12 && !strcmp(name, "speed"));
    asset_source_free(source);

    /* A new component line completes component names rather than the fields
     * of the preceding inline component, even while the document is incomplete. */
    for (int i = 0; i < registry_type_count(); i++) {
        const type_desc *type = registry_type(i);
        if (!type->field_count) {
            continue;
        }
        char draft[512];
        snprintf(
            draft,
            sizeof draft,
            "components {\n    %s %s=0\n    %.1s",
            type->name,
            type->fields[0].name,
            type->name
        );
        source = asset_source_parse(draft);
        asset_source_completions completions;
        asset_source_complete(source, (int)strlen(draft), "/content/prototypes/Test.kdl", NULL, 0, &completions);
        bool found = false;
        for (int choice = 0; choice < acount(completions.items); choice++) {
            found |= !strcmp(completions.items[choice].label, type->name);
        }
        assert(found);
        asset_source_completions_free(&completions);
        asset_source_free(source);
        break;
    }

    if (registry_type_by_name("GameTransform")) {
        const char
            *draft = "version 1\nname \"Sandbox\"\n(array)entities {\n    - {\n"
                     "        name \"Player\"\n        prototype \"Player\"\n  "
                     "      components {\n"
                     "            GAME.GameTransform x=-320.0 y\n        }\n   "
                     " }\n}\n";
        int at = (int)(strstr(draft, "-320.0 y") - draft) + 8;
        source = asset_source_parse(draft);
        asset_source_completions completions;
        asset_source_complete(source, at, "/content/scenes/Sandbox.kdl", NULL, 0, &completions);
        bool found = false;
        for (int i = 0; i < acount(completions.items); i++) {
            found |= !strcmp(completions.items[i].label, "y");
        }
        assert(found);
        asset_source_completions_free(&completions);
        asset_source_free(source);
    }

    /* Accepting a completion from a pack whose identity contains spaces must
     * insert a quoted KDL name, which still resolves its reflected fields. */
    for (int p = 0; p < REGISTRY_PACK_COUNT; p++) {
        const registry_pack *pack = REGISTRY_PACKS[p];
        if (!strchr(pack->name, ' ') || !pack->type_count) {
            continue;
        }
        const type_desc *type = pack->types[0];
        char qualified[256], draft[512];
        snprintf(qualified, sizeof qualified, "%s.%s", pack->name, type->name);
        snprintf(draft, sizeof draft, "components {\n    %.1s", pack->name);
        source = asset_source_parse(draft);
        asset_source_completions completions;
        asset_source_complete(source, (int)strlen(draft), "/content/prototypes/Test.kdl", NULL, 0, &completions);
        const asset_source_completion *choice = NULL;
        for (int i = 0; i < acount(completions.items); i++) {
            if (!strcmp(completions.items[i].label, qualified)) {
                choice = &completions.items[i];
            }
        }
        assert(choice && choice->insert && choice->insert[0] == '"');
        snprintf(draft, sizeof draft, "components {\n    %s\n}\n", choice->insert);
        av_doc *accepted = av_doc_read(draft, error, sizeof error);
        assert(accepted && av_obj_get(av_obj_get(av_doc_root(accepted), "components"), qualified));
        av_doc_free(accepted);
        if (type->field_count) {
            snprintf(
                draft,
                sizeof draft,
                "components {\n    %s %.1s",
                choice->insert,
                type->fields[0].name
            );
            asset_source *context_source = asset_source_parse(draft);
            asset_source_completions fields;
            asset_source_complete(
                context_source,
                (int)strlen(draft),
                "/content/prototypes/Test.kdl",
                NULL,
                0,
                &fields
            );
            bool found = false;
            for (int i = 0; i < acount(fields.items); i++) {
                found |= !strcmp(fields.items[i].label, type->fields[0].name);
            }
            assert(found);
            asset_source_completions_free(&fields);
            asset_source_free(context_source);
        }
        asset_source_completions_free(&completions);
        asset_source_free(source);
        break;
    }

    /* Canonical qualified component identities resolve through their declared
     * reflection pack for value completion and field diagnostics. */
    for (int p = 0; p < REGISTRY_PACK_COUNT; p++) {
        const registry_pack *pack = REGISTRY_PACKS[p];
        for (int i = 0; i < pack->type_count; i++) {
            const type_desc *type = pack->types[i];
            for (int f = 0; f < type->field_count; f++) {
                const type_field *field = &type->fields[f];
                if (field->type != PIN_BOOL) {
                    continue;
                }
                char draft[512], qualified[256];
                snprintf(qualified, sizeof qualified, "%s.%s", pack->name, type->name);
                av_doc *literal_doc = av_doc_new();
                char *type_literal = av_value_write(av_str(literal_doc, qualified));
                av_doc_free(literal_doc);
                snprintf(
                    draft,
                    sizeof draft,
                    "components {\n    %s %s=#t",
                    type_literal,
                    field->name
                );
                source = asset_source_parse(draft);
                asset_source_completions completions;
                asset_source_complete(
                    source,
                    (int)strlen(draft),
                    "/content/prototypes/Test.kdl",
                    NULL,
                    0,
                    &completions
                );
                bool found = false;
                for (int choice = 0; choice < acount(completions.items); choice++) {
                    found |= !strcmp(completions.items[choice].label, "#true");
                }
                assert(found);
                asset_source_completions_free(&completions);
                asset_source_free(source);
                snprintf(
                    draft,
                    sizeof draft,
                    "components {\n    %s %s=1\n}\n",
                    type_literal,
                    field->name
                );
                source = asset_source_parse(draft);
                assert(source->document && acount(source->diagnostics));
                asset_source_free(source);
                free(type_literal);
                return;
            }
        }
    }
}
