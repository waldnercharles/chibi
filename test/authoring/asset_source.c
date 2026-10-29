#include "assets/asset_source.h"

#include "graph/model/registry.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "test/support/graph_packs.h"
#include "third_party/pico_headers/pico_unit.h"

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

/* Executable source facts come from the shared canonical frontend. KDL
 * property completion remains independently covered below. */
/* Completion offers the properties a component has not set yet, through
 * qualified and quoted names, braced fields and incomplete drafts. */
TEST_CASE(asset_source_completes_unset_properties)
{
    REQUIRE(test_graph_packs_set());
    /* Use declared packs so qualified and quoted names exercise the same
     * reflection metadata as loaded projects, including incomplete drafts. */
    for (int p = 0; p < registry_pack_count(); p++) {
        const registry_pack *pack = registry_pack_at(p);
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
            REQUIRE(!has_completion(&choices, a) && has_completion(&choices, b));
            asset_source_completions_free(&choices);
            asset_source_free(source);

            /* Properties after the caret are also occupied. A partial key
             * before them remains editable rather than excluding itself. */
            snprintf(draft, sizeof draft, "components {\n %s %s=0  %s=0\n}\n", literal, a, b);
            int at = (int)(strstr(draft, "=0  ") - draft) + 4;
            source = asset_source_parse(draft);
            asset_source_complete(source, at, "Test.kdl", NULL, 0, &choices);
            REQUIRE(!has_completion(&choices, a) && !has_completion(&choices, b));
            asset_source_completions_free(&choices);
            at = (int)(strstr(draft, "=0  ") - draft) + 4 + (int)strlen(b);
            asset_source_complete(source, at, "Test.kdl", NULL, 0, &choices);
            REQUIRE(has_completion(&choices, b));
            asset_source_completions_free(&choices);
            asset_source_free(source);

            /* Braced fields exclude siblings, while a second component gets
             * a fresh property set even if the previous component used it. */
            snprintf(draft, sizeof draft, "components {\n %s {\n %s 0\n ", literal, a);
            source = asset_source_parse(draft);
            asset_source_complete(source, (int)strlen(draft), "Test.kdl", NULL, 0, &choices);
            REQUIRE(!has_completion(&choices, a) && has_completion(&choices, b));
            asset_source_completions_free(&choices);
            asset_source_free(source);
            snprintf(draft, sizeof draft, "components {\n %s %s=0\n %s ", literal, a, literal);
            source = asset_source_parse(draft);
            asset_source_complete(source, (int)strlen(draft), "Test.kdl", NULL, 0, &choices);
            REQUIRE(has_completion(&choices, a) && has_completion(&choices, b));
            asset_source_completions_free(&choices);
            asset_source_free(source);
            /* An invalid numeric draft must not hide property completion or
             * forget which key is already present. */
            snprintf(draft, sizeof draft, "components {\n %s %s=0oops ", literal, a);
            source = asset_source_parse(draft);
            asset_source_complete(source, (int)strlen(draft), "Test.kdl", NULL, 0, &choices);
            REQUIRE(!has_completion(&choices, a) && has_completion(&choices, b));
            asset_source_completions_free(&choices);
            asset_source_free(source);

            for (int f = 0; f < type->field_count; f++) {
                const type_field *field = &type->fields[f];
                const type_desc *nested = field->type == PIN_STRUCT
                                            ? type_catalog_find(field->type_name)
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
                REQUIRE(!has_completion(&choices, nested->fields[0].name));
                REQUIRE(has_completion(&choices, nested->fields[1].name));
                asset_source_completions_free(&choices);
                asset_source_free(source);
                break;
            }
            free(literal);
        }
    }
    return true;
}

/* Reconciling edits into authored source keeps its comments and spacing,
 * replaces only what changed and keeps declaration order. */
TEST_CASE(asset_source_reconcile_keeps_authored_text)
{
    {
        const char *source = "// palette\ncolor 966781183 // water\namount "
                             "0.035000000000000003\n";
        av_doc *values = av_doc_new();
        av_value *root = av_obj(values);
        av_obj_set(values, root, "color", av_color(values, 0x399fe8ff));
        av_obj_set(values, root, "amount", av_float(values, 0.035f));
        char error[256];
        char *saved = asset_source_reconcile(source, root, error, sizeof error);
        REQUIRE(saved && !strcmp(saved, "// palette\ncolor 0x399fe8ff // water\namount 0.035\n"));
        char *again = asset_source_reconcile(saved, root, error, sizeof error);
        REQUIRE(again && !strcmp(saved, again));
        free(again);
        free(saved);
        av_doc_free(values);
    }
    {
        /* Canonical source has nothing authored to keep: an edit writes the
         * target's canonical text, with new members in the target's order. */
        const char *source = "name \"Removable\"\ncomponents {\n"
                             "    (object)Transform\n}\n";
        const char *canonical = "name \"Removable\"\ncomponents {\n"
                                "    (object)Aim\n"
                                "    Transform x=1\n}\n";
        char error[256];
        av_doc *target = av_doc_read(canonical, error, sizeof error);
        REQUIRE(target);
        char *saved = asset_source_reconcile(source, av_doc_root(target), error, sizeof error);
        REQUIRE(saved && !strcmp(saved, canonical));
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
    REQUIRE(doc);
    av_value *root = av_doc_root(doc);
    av_value *body = av_obj_get(av_obj_get(root, "components"), "Body");
    av_obj_set(doc, body, "speed", av_sint(doc, 24));
    char *edited = asset_source_reconcile(text, root, error, sizeof error);
    REQUIRE(edited);
    REQUIRE(!strcmp(
        edited,
        "// authored header\nname  \"Player\" // retain this\n"
        "components {\n  Body speed=24 enabled=#true // tuning\n}\n"
    ));
    free(edited);
    av_obj_add_str(doc, root, "identity", "player");
    edited = asset_source_reconcile(text, root, error, sizeof error);
    REQUIRE(edited && strstr(edited, "// authored header") && strstr(edited, "// tuning"));
    av_doc *check = av_doc_read(edited, error, sizeof error);
    REQUIRE(check && av_equals(av_doc_root(check), root));
    av_doc_free(check);
    free(edited);
    av_obj_remove_key(body, "enabled");
    av_obj_add_int(doc, body, "mass", 5);
    edited = asset_source_reconcile(text, root, error, sizeof error);
    REQUIRE(edited && strstr(edited, "// tuning"));
    check = av_doc_read(edited, error, sizeof error);
    REQUIRE(check && av_equals(av_doc_root(check), root));
    av_doc_free(check);
    free(edited);
    av_doc_free(doc);

    /* Changing a list's length replaces its node, including its annotation,
     * while retaining the surrounding and trailing comments exactly. */
    text = "// list header\n(array)items 1 2 // list note\n";
    doc = av_doc_read(text, error, sizeof error);
    REQUIRE(doc);
    root = av_doc_root(doc);
    av_arr_append(av_obj_get(root, "items"), av_sint(doc, 3));
    edited = asset_source_reconcile(text, root, error, sizeof error);
    REQUIRE(edited && !strcmp(edited, "// list header\n(array)items 1 2 3 // list note\n"));
    free(edited);
    av_doc_free(doc);

    text = "// list header\n(array)items {\n  - value=1 // keep one\n  - "
           "value=2 // keep two\n} // list note\n";
    doc = av_doc_read(text, error, sizeof error);
    root = av_doc_root(doc);
    av_value *objects = av_obj_get(root, "items");
    av_value *third = av_arr_add_obj(doc, objects);
    av_obj_add_int(doc, third, "value", 3);
    edited = asset_source_reconcile(text, root, error, sizeof error);
    REQUIRE(
        edited && strstr(edited, "// keep one") &&
        strstr(edited, "// keep two") && strstr(edited, "// list note")
    );
    check = av_doc_read(edited, error, sizeof error);
    REQUIRE(check && av_equals(av_doc_root(check), root));
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
    REQUIRE(edited && strstr(edited, "// header") && strstr(edited, "// tuning"));
    check = av_doc_read(edited, error, sizeof error);
    REQUIRE(check && av_equals(av_doc_root(check), root));
    av_doc_free(check);
    free(edited);
    av_obj_set(doc, root, "Body", av_bool(doc, true));
    edited = asset_source_reconcile(text, root, error, sizeof error);
    REQUIRE(edited && !strcmp(edited, "// header\nBody #true // tuning\n"));
    free(edited);
    av_doc_free(doc);
    text = "name  \"Player\" // retain this\n";
    doc = av_doc_read(text, error, sizeof error);
    root = av_doc_root(doc);
    av_value *list = av_obj_add_arr(doc, root, "name");
    av_arr_add_str(doc, list, "Player");
    edited = asset_source_reconcile(text, root, error, sizeof error);
    REQUIRE(edited && strstr(edited, "(array)name") && strstr(edited, "// retain this"));
    check = av_doc_read(edited, error, sizeof error);
    REQUIRE(check && av_equals(av_doc_root(check), root));
    av_doc_free(check);
    free(edited);
    av_doc_free(doc);

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
    REQUIRE(doc && target);
    edited = asset_source_reconcile(text, av_doc_root(target), error, sizeof error);
    REQUIRE(edited && strstr(edited, "Points id=103 type=int // payload"));
    check = av_doc_read(edited, error, sizeof error);
    av_value *outputs = av_obj_get(av_doc_root(check), "outputs");
    REQUIRE(!strcmp(av_get_str(av_obj_key_at(outputs, 1)), "Points"));
    REQUIRE(!strcmp(av_get_str(av_obj_key_at(outputs, 2)), "Source"));
    av_doc_free(check);
    av_doc_free(target);
    av_doc_free(doc);
    free(edited);

    return true;
}

/* Incomplete source still highlights the edited token and marks an error;
 * value locations identify a field however it is formatted. */
TEST_CASE(asset_source_parses_incomplete_drafts)
{
    const char
        *text = "// authored header\nname  \"Player\" // retain this\n"
                "components {\n  Body speed=12 enabled=#true // tuning\n}\n";
    asset_source *source = asset_source_parse("name \"unterminated");
    REQUIRE(!source->document && acount(source->diagnostics) > 0);
    REQUIRE(acount(source->spans) == 2);
    REQUIRE(!strcmp(source->spans[1].cls, "tok-string"));
    asset_source_free(source);

    /* Value locations identify the field independent of formatting. */
    source = asset_source_parse(text);
    const char *name = NULL;
    const av_value *value = asset_source_value_at(source, (int)(strstr(text, "12") - text), &name);
    REQUIRE(value && av_get_int(value) == 12 && !strcmp(name, "speed"));
    asset_source_free(source);
    return true;
}

/* Completion names components on a new line, fields after a value, quoted
 * qualified names that resolve their fields, and boolean values; a wrongly
 * typed value is a diagnostic. */
TEST_CASE(asset_source_completes_components_fields_and_values)
{
    REQUIRE(test_graph_packs_set());
    asset_source *source;
    char error[256];

    /* A new component line completes component names rather than the fields
     * of the preceding inline component, even while the document is incomplete. */
    for (int i = 0; i < type_catalog_count(); i++) {
        const type_desc *type = type_catalog_at(i);
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
        REQUIRE(found);
        asset_source_completions_free(&completions);
        asset_source_free(source);
        break;
    }

    /* A scene entity's component block completes the component's fields. */
    if (type_catalog_find("Transform")) {
        const char
            *draft = "version 1\nname \"Sandbox\"\n(array)entities {\n    - {\n"
                     "        name \"Player\"\n        prototype \"Player\"\n  "
                     "      components {\n"
                     "            Transform x=-320.0 y\n        }\n   "
                     " }\n}\n";
        int at = (int)(strstr(draft, "-320.0 y") - draft) + 8;
        source = asset_source_parse(draft);
        asset_source_completions completions;
        asset_source_complete(source, at, "/content/scenes/Sandbox.kdl", NULL, 0, &completions);
        bool found = false;
        for (int i = 0; i < acount(completions.items); i++) {
            found |= !strcmp(completions.items[i].label, "y");
        }
        REQUIRE(found);
        asset_source_completions_free(&completions);
        asset_source_free(source);
    }

    /* Accepting a completion from a pack whose identity contains spaces must
     * insert a quoted KDL name, which still resolves its reflected fields. */
    for (int p = 0; p < registry_pack_count(); p++) {
        const registry_pack *pack = registry_pack_at(p);
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
        REQUIRE(choice && choice->insert && choice->insert[0] == '"');
        snprintf(draft, sizeof draft, "components {\n    %s\n}\n", choice->insert);
        av_doc *accepted = av_doc_read(draft, error, sizeof error);
        REQUIRE(
            accepted && av_obj_get(av_obj_get(av_doc_root(accepted), "components"), qualified)
        );
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
            REQUIRE(found);
            asset_source_completions_free(&fields);
            asset_source_free(context_source);
        }
        asset_source_completions_free(&completions);
        asset_source_free(source);
        break;
    }

    /* Canonical qualified component identities resolve through their declared
     * reflection pack for value completion and field diagnostics. */
    for (int p = 0; p < registry_pack_count(); p++) {
        const registry_pack *pack = registry_pack_at(p);
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
                REQUIRE(found);
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
                REQUIRE(source->document && acount(source->diagnostics));
                asset_source_free(source);
                free(type_literal);
                return true;
            }
        }
    }
    REQUIRE(!"a reflected component has a boolean field");
    return true;
}

/* Scene files use the component descriptor's identity, independently of the
 * synthetic pack that presents the native types in the graph palette. */
TEST_CASE(asset_source_resolves_canonical_component_identities)
{
    REQUIRE(test_graph_packs_set());
    const char *names[] = { "Transform", "Wrong.Transform" };
    for (int i = 0; i < 2; i++) {
        char draft[256];
        snprintf(draft, sizeof draft, "components {\n %s x=1 y\n}\n", names[i]);
        asset_source *source = asset_source_parse(draft);
        int at = (int)(strstr(draft, "x=1 y") - draft) + 5;
        asset_source_completions choices;
        asset_source_complete(source, at, "Scene.kdl", NULL, 0, &choices);
        REQUIRE(has_completion(&choices, "y") == (i == 0));
        asset_source_completions_free(&choices);
        asset_source_free(source);

        snprintf(draft, sizeof draft, "components {\n %s x=1 y=2\n}\n", names[i]);
        source = asset_source_parse(draft);
        at = (int)(strstr(draft, "x=1") - draft) + 2;
        char *hover = asset_source_hover(source, at);
        REQUIRE(hover);
        REQUIRE((strstr(hover, "x: number") != NULL) == (i == 0));
        free(hover);
        asset_source_free(source);
    }
    /* A wrong value names the field's type as the language spells it. */
    asset_source *wrong = asset_source_parse("components {\n Transform x=#true\n}\n");
    REQUIRE(acount(wrong->diagnostics) == 1);
    REQUIRE(strstr(wrong->diagnostics[0].message, "Transform.x (number)"));
    asset_source_free(wrong);
    /* Typing a component offers the key scene files name it by. */
    const char *draft = "components {\n T\n}\n";
    asset_source *source = asset_source_parse(draft);
    asset_source_completions choices;
    asset_source_complete(source, (int)(strstr(draft, "T") - draft) + 1, "Scene.kdl", NULL, 0, &choices);
    REQUIRE(has_completion(&choices, "Transform"));
    asset_source_completions_free(&choices);
    asset_source_free(source);
    return true;
}

void test_asset_source()
{
    pu_setup(NULL, test_graph_packs_clear);
    RUN_TEST_CASE(asset_source_completes_unset_properties);
    RUN_TEST_CASE(asset_source_resolves_canonical_component_identities);
    RUN_TEST_CASE(asset_source_reconcile_keeps_authored_text);
    RUN_TEST_CASE(asset_source_parses_incomplete_drafts);
    RUN_TEST_CASE(asset_source_completes_components_fields_and_values);
    pu_clear_setup();
}
