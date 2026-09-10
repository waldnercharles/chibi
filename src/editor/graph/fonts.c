#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"

#include <assert.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <float.h>
#include <imgui/dcimgui_internal.h>
#include <stdint.h>
#include <string.h>

extern const unsigned char editor_ui_font[], editor_ui_bold_font[],
    editor_nerd_symbols_font[];
extern const size_t editor_ui_font_size, editor_ui_bold_font_size,
    editor_nerd_symbols_font_size;
ImFont *ed_find_font(const char *name)
{
    ImFontAtlas *atlas = ImGui_GetIO()->Fonts;
    for (int i = 0; i < atlas->Fonts.Size; i++) {
        ImFont *font = atlas->Fonts.Data[i];
        if (!strcmp(ImFont_GetDebugName(font), name)) {
            return font;
        }
    }
    return NULL;
}

/* ImGui retains GlyphRanges as well as font bytes. Store both in its owned
 * allocation so neither points into an unloaded application generation. */
static void *ed_font_copy(const unsigned char *data, size_t size, ImFontConfig *config)
{
    size_t count = 0;
    if (config->GlyphRanges) {
        while (config->GlyphRanges[count]) {
            count++;
        }
        count++;
    }
    size_t offset = (size + alignof(ImWchar) - 1) & ~(alignof(ImWchar) - 1);
    unsigned char *copy = ImGui_MemAlloc(offset + count * sizeof(ImWchar));
    assert(copy);
    memcpy(copy, data, size);
    if (count) {
        memcpy(copy + offset, config->GlyphRanges, count * sizeof(ImWchar));
        config->GlyphRanges = (const ImWchar *)(copy + offset);
    }
    return copy;
}

static ImFont *ed_load_icon_font(void)
{
    ImFont *existing = ed_find_font("Chibi Body");
    if (existing) {
        return existing;
    }
    ImFontAtlas *atlas = ImGui_GetIO()->Fonts;
    ImFontConfig text_config = { 0 };
    text_config.FontDataOwnedByAtlas = true;
    strcpy(text_config.Name, "Chibi Body");
    text_config.GlyphMaxAdvanceX = FLT_MAX;
    text_config.RasterizerMultiply = 1.0f;
    text_config.RasterizerDensity = 1.0f;
    text_config.ExtraSizeScale = 1.0f;
    ImGui_GetIO()->FontDefault = ImFontAtlas_AddFontFromMemoryTTF(
        atlas,
        ed_font_copy(editor_ui_font, editor_ui_font_size, &text_config),
        (int)editor_ui_font_size,
        16.0f,
        &text_config,
        NULL
    );
    ImGui_GetStyle()->FontSizeBase = 16.0f;
    ImFontConfig config = { 0 };
    config.FontDataOwnedByAtlas = true;
    config.MergeMode = true;
    const ImWchar ranges[] = { EDITOR_ICON_CODICON_CHEVRON_DOWN,
                               EDITOR_ICON_CODICON_CHEVRON_UP,
                               EDITOR_ICON_CODICON_CHEVRON_LEFT,
                               EDITOR_ICON_CODICON_CHEVRON_LEFT,
                               EDITOR_ICON_CODICON_CHEVRON_RIGHT,
                               EDITOR_ICON_CODICON_CHEVRON_RIGHT,
                               0xf000,
                               0xf385,
                               0xf400,
                               0xf533,
                               EDITOR_ICON_MD_ARROW_TOP_RIGHT,
                               EDITOR_ICON_MD_ARROW_TOP_RIGHT,
                               0 };
    config.GlyphRanges = ranges;
    config.GlyphMaxAdvanceX = FLT_MAX;
    config.RasterizerMultiply = 1.0f;
    config.RasterizerDensity = 1.0f;
    config.ExtraSizeScale = 1.0f;
    return ImFontAtlas_AddFontFromMemoryTTF(
        ImGui_GetIO()->Fonts,
        ed_font_copy(editor_nerd_symbols_font, editor_nerd_symbols_font_size, &config),
        (int)editor_nerd_symbols_font_size,
        16.0f,
        &config,
        NULL
    );
}

static ImFont *ed_load_math_font(void)
{
    ImFont *existing = ed_find_font("Chibi Math");
    if (existing) {
        return existing;
    }
    ImFontConfig config = { 0 };
    config.FontDataOwnedByAtlas = true;
    strcpy(config.Name, "Chibi Math");
    const ImWchar ranges[] = { 0x0020, 0x00FF, 0x2212, 0x2212, 0x2260, 0x2265, 0 };
    config.GlyphRanges = ranges;
    config.GlyphMaxAdvanceX = FLT_MAX;
    config.RasterizerMultiply = 1.0f;
    config.RasterizerDensity = 1.0f;
    config.ExtraSizeScale = 1.0f;
    return ImFontAtlas_AddFontFromMemoryTTF(
        ImGui_GetIO()->Fonts,
        ed_font_copy(editor_ui_bold_font, editor_ui_bold_font_size, &config),
        (int)editor_ui_bold_font_size,
        16.0f,
        &config,
        NULL
    );
}

void editor_initialize_fonts()
{
    ed_load_icon_font();
    ed_load_math_font();
}
