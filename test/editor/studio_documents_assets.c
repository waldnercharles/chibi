/* The Assets area. */

#include "assets/ui_events.h"
#include "editor/collab/collab.h"
#include "editor/studio/studio_designer_internal.h"
#include "editor/studio/studio_graph_internal.h"
#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/input.h"
#include "graph/document/serialize.h"
#include "graph/nodes/component_nodes.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/screenshot.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "support/text_overlay.h"
#include "test/editor/studio_documents.h"
#include "test/suite_internal.h"
#include "ui/input_feel.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The index is rebuilt in place even when no dirty-generation change preceded
 * an explicit refresh. Numeric row caches must follow completed rebuilds. */
static void assets_cache_follows_explicit_refresh()
{
    studio_fixture f = studio_project_open();
    studio *s = f.studio;
    s->maximized = STUDIO_AREA_TOP;
    studio_active_layout(s)->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_ASSETS;
    s->prefs.assets_tile = STUDIO_ASSETS_LIST;
    s->applied = s->prefs;
    frames(&f, 6);
    reference_index_add_asset(f.scene->asset_index, REFERENCE_ASSET_BEHAVIOR, "QaRefreshCache", NULL, "qa-cache-probe");
    /* The fixture inserts directly into the index; start its cached model
     * afresh, as a completed rebuild would. */
    studio_assets_free(s);
    snprintf(s->assets_filter, sizeof s->assets_filter, "QaRefreshCache");
    s->ui_schedule.wake = true;
    frames(&f, 3);
    char first[4096];
    int rows = list_rows(s, "#assets-list > .list-spacer", first, sizeof first);
    bool found = rows == 2 && strstr(first, "Behaviors");
    unsigned generation = f.scene->asset_index_generation;
    uint64_t revision = f.scene->asset_index_revision;
    s->ui_schedule.next = s->time + 1;
    s->active_until = 0;
    uint64_t emitted = s->ui_schedule.frames;
    assert(app_scene_authoring_refresh_asset_index(f.scene) == 0);
    frame(&f, false);
    rows = list_rows(s, "#assets-list > .list-spacer", first, sizeof first);
    if (!found || rows || generation != f.scene->asset_index_generation ||
        f.scene->asset_index_revision != revision + 1 ||
        s->ui_schedule.frames != emitted + 1) {
        printf(
            "  Assets cache: found=%d rows=%d generation=%u/%u "
            "revision=%llu/%llu emissions=%llu/%llu first='%s'\n",
            found,
            rows,
            generation,
            f.scene->asset_index_generation,
            (unsigned long long)revision,
            (unsigned long long)f.scene->asset_index_revision,
            (unsigned long long)emitted,
            (unsigned long long)s->ui_schedule.frames,
            first
        );
    }
    test_check(
        found && !rows && generation == f.scene->asset_index_generation &&
            f.scene->asset_index_revision == revision + 1 &&
            s->ui_schedule.frames == emitted + 1,
        "Assets discards cached rows after an explicit index rebuild with "
        "unchanged dirty generation"
    );
    fixture_free(&f);
}

void studio_documents_assets_checks()
{
    assets_cache_follows_explicit_refresh();
}
