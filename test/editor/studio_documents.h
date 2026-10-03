/* Studio over a project on disk, in a hidden window: the fixture and input
 * helpers the surface checks share (studio_documents.c), and each surface's
 * checks, one file per surface (studio_documents_<surface>.c). */
#ifndef CHIBI_TEST_STUDIO_DOCUMENTS_H
#define CHIBI_TEST_STUDIO_DOCUMENTS_H

#include "editor/studio/studio_internal.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>

#define FRAME_SECONDS (1.0 / 60.0)
#define PROJECT "test/fixtures/studio"
/* Weapons' Ignore Shooter function and its automatic component setter. The
   setter's file entry, {id: 10247, type: Set_GameCollider_ignore_entity},
   stays one line wherever the canvas arranges it. */
#define FUNCTION_GRAPH 1
#define SETTER 10247
#define VAULT PROJECT "/content/scenes/Vault.kdl"
#define BANK PROJECT "/content/ui/Bank.rml"

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    engine_viewport *viewport;
    app_scene_authoring *scene;
    studio *studio;
    double time;
} studio_fixture;

/* ---------- Fixtures ---------- */

studio_fixture fixture_open(const char *project, const char *scene, const char *behavior);
studio_fixture fixture_new();
studio_fixture scene_open(const char *project, const char *scene, const char *behavior);
studio_fixture vault_open();
studio_fixture studio_project_open();
studio_fixture side_open();
void fixture_free(studio_fixture *f);

/* ---------- Frames ---------- */

void frame_delta(studio_fixture *f, bool reload, double delta_seconds);
void frame(studio_fixture *f, bool reload);
void frames(studio_fixture *f, int count);

/* ---------- Input ---------- */

void key_event_mod(SDL_Scancode key, bool down, SDL_Keymod mod, bool repeat);
void key_event(SDL_Scancode key, bool down);
void tap(SDL_Scancode key);
void text_event(const char *text);
void mouse_move(const studio *s, float x, float y);
void graph_drag_button(const studio *s, float x, float y, bool down);
void press(studio *s, rml_element *el);

/* ---------- Elements ---------- */

rml_element *studio_element(studio *s, const char *selector);
int panel_elements(studio *s, const char *panel, const char *selector, rml_element **found);
int list_rows(studio *s, const char *selector, char *first, size_t size);
int tab_named(const app_scene_authoring *scene, const char *label);

/* ---------- Surfaces ---------- */

void studio_documents_graph_checks();
void studio_documents_shell_checks();
void studio_documents_panel_checks();
void studio_documents_outliner_checks();
/* The fields alone, for --studio-properties. */
void studio_documents_property_field_checks();
void studio_documents_properties_checks();
void studio_documents_assets_checks();

#endif
