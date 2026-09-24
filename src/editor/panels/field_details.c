#include "editor/panels/field_details.h"

#include "editor/workspace/editor_internal.h"
#include "graph/compiler/field_program.h"
#include "graph/document/library.h"

#include <imgui/dcimgui.h>
#include <math.h>

void ed_draw_field_details(editor *e)
{
    if (e->asset->domain != BEHAVIOR_DOMAIN_FIELD) {
        return;
    }
    uint64_t catalog = 0;
    for (int i = 0; i < acount(e->asset_catalog); i++) {
        const behavior_asset *asset = e->asset_catalog[i];
        catalog = (catalog ^ asset->identity) * UINT64_C(1099511628211);
        for (int g = 0; g < acount(asset->graphs); g++) {
            catalog = (catalog ^ graph_topology_revision(asset->graphs[g])) *
                      UINT64_C(1099511628211);
        }
    }
    bool changed = e->field_preview_identity != e->asset->identity ||
                   e->field_preview_revision != e->edit_revision ||
                   e->field_preview_catalog != catalog;
    if (!e->field_cell_size) {
        e->field_cell_size = 8;
        changed = true;
    }
    ImGui_TextUnformatted("Procedural Field");
    changed |= ImGui_DragFloatEx("Cell size", &e->field_cell_size, .1f, .01f, 64, "%.2f", 0);
    changed |= ImGui_InputInt("Seed", &e->field_seed);
    changed |= ImGui_Button("Bake");
    if (changed) {
        e->field_preview_identity = e->asset->identity;
        e->field_preview_catalog = catalog;
        e->field_preview_revision = e->edit_revision;
        e->field_error[0] = 0;
        behavior_asset *linked = behavior_asset_link_libraries(
            e->asset,
            e->asset_catalog,
            acount(e->asset_catalog),
            e->field_error,
            sizeof e->field_error
        );
        field_program *
            program = linked
                        ? field_program_compile(linked, e->field_error, sizeof e->field_error)
                        : NULL;
        e->field_preview_valid = program &&
                                 field_program_bake(
                                     program,
                                     (field_context){ .seed = (unsigned)e->field_seed },
                                     (field_grid){ 64, 64, e->field_cell_size },
                                     e->field_values,
                                     NULL,
                                     e->field_error,
                                     sizeof e->field_error
                                 );
        field_program_free(program);
        behavior_asset_free(linked);
    }
    if (!e->field_preview_valid) {
        ImGui_TextWrapped("%s", e->field_error);
        return;
    }
    ImVec2 origin = ImGui_GetCursorScreenPos();
    float side = fmaxf(1, ImGui_GetContentRegionAvail().x), step = side / 64;
    ImDrawList *draw = ImGui_GetWindowDrawList();
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            float value = e->field_values[(63 - y) * 64 + x];
            float edge = expf(-fabsf(value) / e->field_cell_size);
            ImVec4 color = value <= 0 ? (ImVec4){ .22f, .61f, .71f, 1 }
                                      : (ImVec4){ .07f, .09f, .14f, 1 };
            color.x += (.94f - color.x) * edge;
            color.y += (.94f - color.y) * edge;
            color.z += (.94f - color.z) * edge;
            ImDrawList_AddRectFilled(
                draw,
                (ImVec2){ origin.x + x * step, origin.y + y * step },
                (ImVec2){ origin.x + (x + 1) * step, origin.y + (y + 1) * step },
                ImGui_ColorConvertFloat4ToU32(color)
            );
        }
    }
    ImGui_Dummy((ImVec2){ side, side });
}
