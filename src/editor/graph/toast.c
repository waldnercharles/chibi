#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"

#include <assert.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <stdint.h>

void ed_draw_centered_icon(
    ImDrawList *dl,
    ImFont *font,
    float font_size,
    ImVec2 min,
    ImVec2 max,
    ImU32 color,
    ImWchar codepoint
)
{
    ImFontBaked *baked = ImFont_GetFontBaked(font, font_size);
    ImFontGlyph *glyph = ImFontBaked_FindGlyphNoFallback(baked, codepoint);
    assert(glyph);
    float scale = font_size / baked->Size;
    float width = (glyph->X1 - glyph->X0) * scale;
    float height = (glyph->Y1 - glyph->Y0) * scale;
    ImVec2 pos = { min.x + (max.x - min.x - width) * 0.5f - glyph->X0 * scale,
                   min.y + (max.y - min.y - height) * 0.5f - glyph->Y0 * scale };
    ImFont_RenderChar(font, dl, font_size, pos, color, codepoint);
}

int ed_connect_error_rect(const editor *e, ImVec2 *min, ImVec2 *max, ImVec2 *close_min, ImVec2 *close_max)
{
    if (!e->connect_error[0] ||
        e->monotonic_time >= e->connect_error_until + ED_TOAST_FADE) {
        return 0;
    }
    float width = e->canvas_size.x < ED_TOAST_WIDTH ? e->canvas_size.x : ED_TOAST_WIDTH;
    float text_width = width - ED_TOAST_PAD * 2.0f - ED_TOAST_ICON -
                       ED_TOAST_GAP - ED_TOAST_CLOSE;
    float font_size = ImGui_GetFontSize();
    if (text_width < font_size) {
        text_width = font_size;
    }
    float text_height = ImGui_CalcTextSizeEx(e->connect_error, NULL, false, text_width)
                            .y;
    float height = ED_TOAST_PAD * 2.0f;
    height += text_height > ED_TOAST_ICON ? text_height : ED_TOAST_ICON;
    ImVec2 canvas_max = ed_add(e->canvas_min, e->canvas_size);
    ImVec2 pos = ed_add(e->connect_error_pos, (ImVec2){ 12.0f, 12.0f });
    if (pos.x + width > canvas_max.x) {
        pos.x = canvas_max.x - width;
    }
    if (pos.y + height > canvas_max.y) {
        pos.y = canvas_max.y - height;
    }
    if (pos.x < e->canvas_min.x) {
        pos.x = e->canvas_min.x;
    }
    if (pos.y < e->canvas_min.y) {
        pos.y = e->canvas_min.y;
    }
    *min = pos;
    *max = ed_add(pos, (ImVec2){ width, height });
    *close_min = (ImVec2){ max->x - ED_TOAST_PAD - ED_TOAST_CLOSE,
                           min->y + ED_TOAST_PAD };
    *close_max = (ImVec2){ max->x - ED_TOAST_PAD, close_min->y + ED_TOAST_CLOSE };
    return 1;
}

void ed_clear_connect_error(editor *e)
{
    e->connect_error[0] = '\0';
}

static int ed_connect_error_alpha(const editor *e)
{
    double remaining = e->connect_error_until - e->monotonic_time;
    if (remaining >= 0.0) {
        return 255;
    }
    double fade_remaining = ED_TOAST_FADE + remaining;
    if (fade_remaining <= 0.0) {
        return 0;
    }
    return (int)(255.0 * fade_remaining / ED_TOAST_FADE);
}

void ed_draw_connect_error(const editor *e)
{
    ImVec2 min, max, close_min, close_max;
    if (!ed_connect_error_rect(e, &min, &max, &close_min, &close_max)) {
        return;
    }
    int alpha = ed_connect_error_alpha(e);
    if (!alpha) {
        return;
    }
    ImDrawList *dl = ImGui_GetWindowDrawList();
    int close_hovered = ed_point_in_rect(ImGui_GetIO()->MousePos, close_min, close_max);
    if (close_hovered) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList_PushClipRect(dl, e->canvas_min, ed_add(e->canvas_min, e->canvas_size), true);
    ImDrawList_AddRectFilledEx(dl, min, max, IM_COL32(24, 27, 34, (int)(alpha * 0.94f)), 6.0f, ImDrawFlags_RoundCornersAll);
    ImDrawList_AddRectEx(dl, min, max, IM_COL32(210, 74, 74, alpha), 6.0f, ImDrawFlags_RoundCornersAll, 1.0f);
    if (e->icon_font) {
        ImDrawList_AddTextImFontPtr(
            dl,
            e->icon_font,
            ED_TOAST_ICON,
            (ImVec2){ min.x + ED_TOAST_PAD, min.y + ED_TOAST_PAD },
            IM_COL32(210, 74, 74, alpha),
            ED_ICON_WARNING
        );
    } else {
        ImVec2 icon_center = { min.x + ED_TOAST_PAD + ED_TOAST_ICON * 0.5f,
                               min.y + ED_TOAST_PAD + ED_TOAST_ICON * 0.5f };
        ImDrawList_AddCircleFilled(dl, icon_center, ED_TOAST_ICON * 0.5f, IM_COL32(210, 74, 74, alpha), 0);
        ImDrawList_AddText(
            dl,
            (ImVec2){ icon_center.x - 2.0f, icon_center.y - 9.0f },
            IM_COL32(255, 242, 242, alpha),
            "!"
        );
    }
    ImDrawList_AddTextImFontPtrEx(
        dl,
        ImGui_GetFont(),
        ImGui_GetFontSize(),
        (ImVec2){ min.x + ED_TOAST_PAD + ED_TOAST_ICON + ED_TOAST_GAP, min.y + ED_TOAST_PAD },
        IM_COL32(255, 224, 224, alpha),
        e->connect_error,
        NULL,
        max.x - min.x - ED_TOAST_PAD * 2.0f - ED_TOAST_ICON - ED_TOAST_GAP - ED_TOAST_CLOSE,
        NULL
    );
    ImDrawList_AddRectFilledEx(
        dl,
        close_min,
        close_max,
        IM_COL32(180, 50, 55, close_hovered ? (int)(alpha * 0.78f) : (int)(alpha * 0.16f)),
        4.0f,
        ImDrawFlags_RoundCornersAll
    );
    ImU32 close_color = IM_COL32(255, close_hovered ? 245 : 190, close_hovered ? 245 : 190, alpha);
    if (e->icon_font) {
        ed_draw_centered_icon(dl, e->icon_font, ED_TOAST_CLOSE, close_min, close_max, close_color, EDITOR_ICON_FA_CLOSE);
    } else {
        ImDrawList_AddLineEx(
            dl,
            ed_add(close_min, (ImVec2){ 5.0f, 5.0f }),
            ed_add(close_max, (ImVec2){ -5.0f, -5.0f }),
            close_color,
            close_hovered ? 2.5f : 2.0f
        );
        ImDrawList_AddLineEx(
            dl,
            ed_add(close_min, (ImVec2){ 5.0f, ED_TOAST_CLOSE - 5.0f }),
            ed_add(close_max, (ImVec2){ -5.0f, -ED_TOAST_CLOSE + 5.0f }),
            close_color,
            close_hovered ? 2.5f : 2.0f
        );
    }
    ImDrawList_PopClipRect(dl);
}
