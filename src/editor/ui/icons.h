#ifndef CBLUEPRINT_EDITOR_ICONS_H
#define CBLUEPRINT_EDITOR_ICONS_H

/* Font-backed UI icons. Names follow the Nerd Fonts cheat sheet prefixes so
 * an agent can search the source by the published icon name or codepoint. */
typedef enum editor_icon
{
    EDITOR_ICON_FA_SEARCH = 0xf002,             /* nf-fa-search */
    EDITOR_ICON_FA_CLOSE = 0xf00d,              /* nf-fa-close */
    EDITOR_ICON_FA_REFRESH = 0xf01e,            /* nf-fa-refresh */
    EDITOR_ICON_FA_PLAY = 0xf04b,               /* nf-fa-play */
    EDITOR_ICON_FA_PAUSE = 0xf04c,              /* nf-fa-pause */
    EDITOR_ICON_FA_STOP = 0xf04d,               /* nf-fa-stop */
    EDITOR_ICON_FA_FAST_FORWARD = 0xf051,       /* nf-fa-fast_forward */
    EDITOR_ICON_FA_CROSSHAIRS = 0xf05b,         /* nf-fa-crosshairs */
    EDITOR_ICON_FA_EYE = 0xf06e,                /* nf-fa-eye */
    EDITOR_ICON_FA_PLUS = 0xf067,               /* nf-fa-plus */
    EDITOR_ICON_FA_WARNING = 0xf071,            /* nf-fa-warning */
    EDITOR_ICON_FA_CODE = 0xf121,               /* nf-fa-code */
    EDITOR_ICON_FA_CODE_FORK = 0xf126,          /* nf-fa-code_fork */
    EDITOR_ICON_FA_UNLINK = 0xf127,             /* nf-fa-unlink */
    EDITOR_ICON_FA_ARROW_LEFT = 0xf060,         /* nf-fa-arrow_left */
    EDITOR_ICON_FA_ARROW_RIGHT = 0xf061,        /* nf-fa-arrow_right */
    EDITOR_ICON_FA_EXPAND = 0xf065,             /* nf-fa-expand */
    EDITOR_ICON_FA_PENCIL = 0xf040,             /* nf-fa-pencil */
    EDITOR_ICON_FA_COG = 0xf013,                /* nf-fa-cog */
    EDITOR_ICON_FA_ELLIPSIS_V = 0xf141,         /* nf-fa-ellipsis_v */
    EDITOR_ICON_FA_BOLT = 0xf0e7,               /* nf-fa-bolt */
    EDITOR_ICON_FA_SITEMAP = 0xf0e8,            /* nf-fa-sitemap */
    EDITOR_ICON_FA_SEND = 0xf1d8,               /* nf-fa-send */
    EDITOR_ICON_FA_DATABASE = 0xf1c0,           /* nf-fa-database */
    EDITOR_ICON_FA_BUG = 0xf188,                /* nf-fa-bug */
    EDITOR_ICON_FA_TRASH = 0xf1f8,              /* nf-fa-trash */
    EDITOR_ICON_FA_UNDO = 0xf0e2,               /* nf-fa-undo */
    EDITOR_ICON_MD_ARROW_TOP_RIGHT = 0xf005c,   /* nf-md-arrow_top_right */
    EDITOR_ICON_CODICON_CHEVRON_DOWN = 0xebf3,  /* nf-cod-chevron_down */
    EDITOR_ICON_CODICON_CHEVRON_UP = 0xebf4,    /* nf-cod-chevron_up */
    EDITOR_ICON_CODICON_CHEVRON_LEFT = 0xec00,  /* nf-cod-chevron_left */
    EDITOR_ICON_CODICON_CHEVRON_RIGHT = 0xec02, /* nf-cod-chevron_right */
} editor_icon;

#endif
