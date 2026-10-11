#include "runtime/ui/builtin_fonts.h"

#include <assert.h>

extern const unsigned char editor_ui_font[];
extern const unsigned char editor_ui_medium_font[];
extern const unsigned char editor_ui_semibold_font[];
extern const unsigned char editor_ui_numeric_font[];
extern const unsigned char editor_ui_bold_font[];
extern const unsigned char editor_nerd_symbols_font[];
extern const unsigned char editor_noto_sans_font[];
extern const unsigned char editor_noto_sans_semibold_font[];
extern const unsigned char editor_noto_sans_bold_font[];
extern const unsigned char editor_inconsolata_font[];
extern const size_t editor_ui_font_size;
extern const size_t editor_ui_medium_font_size;
extern const size_t editor_ui_semibold_font_size;
extern const size_t editor_ui_numeric_font_size;
extern const size_t editor_ui_bold_font_size;
extern const size_t editor_nerd_symbols_font_size;
extern const unsigned char studio_regular_font[];
extern const size_t studio_regular_font_size;
extern const unsigned char studio_medium_font[];
extern const size_t studio_medium_font_size;
extern const unsigned char studio_semibold_font[];
extern const size_t studio_semibold_font_size;
extern const unsigned char studio_bold_font[];
extern const size_t studio_bold_font_size;
extern const unsigned char studio_numeric_font[];
extern const size_t studio_numeric_font_size;
extern const size_t editor_noto_sans_font_size;
extern const size_t editor_noto_sans_semibold_font_size;
extern const size_t editor_noto_sans_bold_font_size;
extern const size_t editor_inconsolata_font_size;
extern const unsigned char editor_code_font[];
extern const size_t editor_code_font_size;

int ui_builtin_font_count()
{
    return 16;
}
ui_font ui_builtin_font_at(int index)
{
    assert(index >= 0 && index < 16);
    switch (index) {
        case 0:
            return (ui_font){ .key = "font/Inter/400/normal",
                              .family = "Inter",
                              .data = editor_ui_font,
                              .length = editor_ui_font_size,
                              .weight = 400,
                              .fallback = true };
        case 1:
            return (ui_font){ .key = "font/Inter/500/normal",
                              .family = "Inter",
                              .data = editor_ui_medium_font,
                              .length = editor_ui_medium_font_size,
                              .weight = 500,
                              .fallback = false };
        case 2:
            return (ui_font){ .key = "font/Inter/600/normal",
                              .family = "Inter",
                              .data = editor_ui_semibold_font,
                              .length = editor_ui_semibold_font_size,
                              .weight = 600,
                              .fallback = false };
        case 3:
            return (ui_font){ .key = "font/Inter/700/normal",
                              .family = "Inter",
                              .data = editor_ui_bold_font,
                              .length = editor_ui_bold_font_size,
                              .weight = 700,
                              .fallback = false };
        case 4:
            return (ui_font){ .key = "font/Noto Sans/400/normal",
                              .family = "Noto Sans",
                              .data = editor_noto_sans_font,
                              .length = editor_noto_sans_font_size,
                              .weight = 400,
                              .fallback = false };
        case 5:
            return (ui_font){ .key = "font/Noto Sans/600/normal",
                              .family = "Noto Sans",
                              .data = editor_noto_sans_semibold_font,
                              .length = editor_noto_sans_semibold_font_size,
                              .weight = 600,
                              .fallback = false };
        case 6:
            return (ui_font){ .key = "font/Noto Sans/700/normal",
                              .family = "Noto Sans",
                              .data = editor_noto_sans_bold_font,
                              .length = editor_noto_sans_bold_font_size,
                              .weight = 700,
                              .fallback = false };
        case 7:
            return (ui_font){ .key = "font/InterNumeric/400/normal",
                              .family = "InterNumeric",
                              .data = editor_ui_numeric_font,
                              .length = editor_ui_numeric_font_size,
                              .weight = 400,
                              .fallback = false };
        case 8:
            return (ui_font){ .key = "font/Geist/400/normal",
                              .family = "Geist",
                              .data = studio_regular_font,
                              .length = studio_regular_font_size,
                              .weight = 400,
                              .fallback = false };
        case 9:
            return (ui_font){ .key = "font/Geist/500/normal",
                              .family = "Geist",
                              .data = studio_medium_font,
                              .length = studio_medium_font_size,
                              .weight = 500,
                              .fallback = false };
        case 10:
            return (ui_font){ .key = "font/Geist/600/normal",
                              .family = "Geist",
                              .data = studio_semibold_font,
                              .length = studio_semibold_font_size,
                              .weight = 600,
                              .fallback = false };
        case 11:
            return (ui_font){ .key = "font/Geist/700/normal",
                              .family = "Geist",
                              .data = studio_bold_font,
                              .length = studio_bold_font_size,
                              .weight = 700,
                              .fallback = false };
        case 12:
            return (ui_font){ .key = "font/GeistNumeric/400/normal",
                              .family = "GeistNumeric",
                              .data = studio_numeric_font,
                              .length = studio_numeric_font_size,
                              .weight = 400,
                              .fallback = false };
        case 13:
            return (ui_font){ .key = "font/Symbols/400/normal",
                              .family = "Symbols",
                              .data = editor_nerd_symbols_font,
                              .length = editor_nerd_symbols_font_size,
                              .weight = 400,
                              .fallback = false };
        case 14:
            return (ui_font){ .key = "font/Inconsolata/400/normal",
                              .family = "Inconsolata",
                              .data = editor_inconsolata_font,
                              .length = editor_inconsolata_font_size,
                              .weight = 400,
                              .fallback = false };
        case 15:
            return (ui_font){ .key = "font/JetBrains Mono/400/normal",
                              .family = "JetBrains Mono",
                              .data = editor_code_font,
                              .length = editor_code_font_size,
                              .weight = 400,
                              .fallback = false };
    }
    return (ui_font){ 0 };
}
