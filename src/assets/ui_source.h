#ifndef CHIBI_UI_SOURCE_H
#define CHIBI_UI_SOURCE_H

#include "support/text_edit.h"

#include <stdbool.h>
#include <stddef.h>

/* A language service for ui assets, as a language server serves an editor:
 * Studio's Text Editor and designer and the MCP read and change ui assets
 * through it. tree-sitter parses an asset's `rml` block with the HTML grammar
 * and its `rcss` block with the CSS grammar.
 *
 * It reads a ui asset's YAML file, or a stylesheet (.rcss) whole. Every
 * position is a byte offset into that text, and every edit a text_edit on
 * it, so results apply to the file as written and keep its formatting. Lines
 * and columns are 1-based. Arrays are support/array.h arrays the source owns.
 */

typedef enum
{
    UI_SOURCE_RML,
    UI_SOURCE_RCSS,
} ui_source_language;

/* A block of markup or style: an asset's rml or rcss value, or a whole
 * stylesheet. */
typedef struct ui_source_block
{
    ui_source_language language;
    int start, end; /* the value's bytes in the text */
    int line;       /* of its first byte */
} ui_source_block;

/* What a span of text is, for highlighting. The first ten are
 * highlight_token's (editor/ui/highlight.h), in its order; the rest refine
 * them, and ui_source_kind_class names both classes. */
typedef enum
{
    UI_SOURCE_TEXT,
    UI_SOURCE_NUMBER,
    UI_SOURCE_STRING,
    UI_SOURCE_KEYWORD,
    UI_SOURCE_NAME,
    UI_SOURCE_PATH,
    UI_SOURCE_PUNCTUATION,
    UI_SOURCE_COMMENT,
    UI_SOURCE_ERROR,
    UI_SOURCE_WARNING,
    UI_SOURCE_TAG,       /* an element's tag; a selector's type */
    UI_SOURCE_ATTRIBUTE, /* an attribute's name */
    UI_SOURCE_SELECTOR,  /* a class or id in a selector */
    UI_SOURCE_PROPERTY,  /* a declaration's property */
    UI_SOURCE_VARIABLE,  /* a custom property, --name */
    UI_SOURCE_BINDING,   /* a name in a data binding: {{ }}, data-if, ... */
    UI_SOURCE_KIND_COUNT,
} ui_source_kind;

typedef struct ui_source_span
{
    int start, end;
    ui_source_kind kind;
} ui_source_span;

/* The span's tok-* classes: its highlight class, then its own when it
 * refines one, as "tok-keyword tok-tag"; NULL for plain text. */
const char *ui_source_kind_class(ui_source_kind kind);

typedef struct ui_source_attribute
{
    int start, end; /* its name through its value's closing quote */
    int name_start, name_end;
    int value_start, value_end; /* inside the quotes; -1 without a value */
} ui_source_attribute;

/* An element of the markup, in document order. */
typedef struct ui_source_element
{
    char tag[64];
    char *id;        /* the id attribute's value, or NULL */
    char *classes;   /* the class attribute's value, or NULL */
    int start, end;  /* its open tag's '<' through its close tag's '>' */
    int open_end;    /* one past its open tag */
    int close_start; /* its close tag's '<'; `end` when it has none */
    bool self_closing;
    int parent; /* index of the element holding it, or -1 */
    int line;
    ui_source_attribute *attributes;
} ui_source_element;

typedef struct ui_source_declaration
{
    int start, end; /* its property through its ';', or its value's end */
    int property_start, property_end;
    int value_start, value_end;
} ui_source_declaration;

/* A rule or at-rule block of the RCSS, in document order. Nested rules keep
 * their selector as written; `parent` holds the enclosing one. */
typedef struct ui_source_rule
{
    char *selector; /* as written, spaces collapsed; "@media ..." for an at-rule */
    int start, end;  /* its selector through its '}' */
    int block_start; /* its '{' */
    int parent;      /* index of the rule holding it, or -1 */
    int line;
    ui_source_declaration *declarations;
} ui_source_rule;

typedef struct ui_source_diagnostic
{
    int start, end;
    int line, column;
    char message[192];
} ui_source_diagnostic;

typedef struct ui_source_tree ui_source_tree;

typedef struct ui_source
{
    char *text;
    ui_source_block *blocks;
    ui_source_span *spans;             /* in order, not overlapping */
    ui_source_element *elements;       /* of the rml block */
    ui_source_rule *rules;             /* of the rcss block */
    ui_source_diagnostic *diagnostics; /* syntax errors, in order */
    ui_source_tree *trees;             /* the parse, private */
} ui_source;

/* Parses a ui asset's file text, or with `stylesheet` a whole .rcss file.
 * Text YAML cannot read still yields what its rml: and rcss: keys hold. */
ui_source *ui_source_parse(const char *text, bool stylesheet);
void ui_source_free(ui_source *source);

/* The 1-based line and column of a byte offset, and back; an offset past
 * the line's end stops at its end. */
void ui_source_position(const ui_source *source, int offset, int *line, int *column);
int ui_source_offset(const ui_source *source, int line, int column);

/* The block holding `offset`, or NULL. */
const ui_source_block *ui_source_block_at(const ui_source *source, int offset);
/* The innermost element whose range holds `offset`, or -1. */
int ui_source_element_at(const ui_source *source, int offset);
/* An element attribute's value as the markup means it, owned; NULL when the
 * element lacks it. */
char *ui_source_attribute_value(const ui_source *source, int element, const char *name);

/* ---------- Project knowledge ---------- */

/* A file completion, definitions and references look through. */
typedef struct ui_source_file
{
    char *path;
    char *name; /* a ui asset's name; NULL for a stylesheet */
} ui_source_file;

/* What the project around a document offers it. ui_source_project_scan
 * fills everything but `properties`, which the caller adds from RmlUi
 * (rml_property_names in ui/rml.h). Lists are sorted and unique. */
typedef struct ui_source_project
{
    ui_source_file *files; /* ui assets, then stylesheets */
    char **widgets;        /* ui asset names */
    char **classes;        /* in selectors and class attributes */
    char **variables;      /* custom properties, with their -- */
    char **properties;     /* RCSS properties */
    char **save_keys;      /* names bindings read after save. */
} ui_source_project;

/* Reads the ui assets and stylesheets under `content_root`/ui, through
 * text_file_read so unsaved text counts. */
void ui_source_project_scan(ui_source_project *project, const char *content_root);
/* Adds `name` to a sorted, unique list, copying it. */
void ui_source_list_add(char ***list, const char *name);
void ui_source_project_free(ui_source_project *project);

/* ---------- Completion ---------- */

typedef enum
{
    UI_COMPLETE_TAG,
    UI_COMPLETE_ATTRIBUTE,
    UI_COMPLETE_WIDGET,
    UI_COMPLETE_CLASS,
    UI_COMPLETE_VARIABLE,
    UI_COMPLETE_PROPERTY,
    UI_COMPLETE_SAVE_KEY,
} ui_source_completion_kind;

typedef struct ui_source_completion
{
    ui_source_completion_kind kind;
    char *label; /* what a choice inserts */
} ui_source_completion;

/* The choices at `offset`, which each replace [start, end), the word typed
 * so far; they start with it, ignoring case. */
typedef struct ui_source_completions
{
    int start, end;
    ui_source_completion *items;
} ui_source_completions;

/* Offers tags after '<'; attributes in a tag, RmlUi's data bindings among
 * them; widget names in <use ui>; classes in class attributes, data-class-
 * and selectors; save keys after save. in bindings; RCSS properties where a
 * declaration starts, in style attributes too; and custom properties in
 * var(). */
void ui_source_complete(
    const ui_source *source,
    int offset,
    const ui_source_project *project,
    ui_source_completions *out
);
void ui_source_completions_free(ui_source_completions *completions);

/* ---------- Definitions and references ---------- */

typedef enum
{
    UI_SYMBOL_NONE,
    UI_SYMBOL_WIDGET,   /* <use ui="Name"> */
    UI_SYMBOL_VARIABLE, /* var(--name) or a --name declaration */
    UI_SYMBOL_CLASS, /* in a class attribute, data-class-name or a selector */
} ui_source_symbol_kind;

typedef struct ui_source_symbol
{
    ui_source_symbol_kind kind;
    char name[128];
    int start, end; /* where the text names it */
} ui_source_symbol;

/* The symbol at `offset`; kind NONE when none is there. */
ui_source_symbol ui_source_symbol_at(const ui_source *source, int offset);

typedef struct ui_source_location
{
    char *path;
    int start, end;
    int line, column;
    bool definition; /* a widget's asset, a declaration, a rule's selector */
} ui_source_location;

/* Appends every place the project's files name the symbol, definitions
 * first. */
void ui_source_references(
    const ui_source_project *project,
    const ui_source_symbol *symbol,
    ui_source_location **locations
);
void ui_source_locations_free(ui_source_location *locations);

/* ---------- Edits ---------- */

/* Where an inserted or moved element goes relative to its target: inside it
 * after its children, or before or after it. A target of -1 is the markup
 * itself. */
typedef enum
{
    UI_SOURCE_INSIDE,
    UI_SOURCE_BEFORE,
    UI_SOURCE_AFTER,
} ui_source_place;

/* Each operation appends the edits that make its change to `edits`
 * (support/array.h), their texts owned by the array until
 * ui_source_edits_free. New lines take the indentation of the lines around
 * them. Returns false with a message in `error` and appends nothing when the
 * change cannot be made. Edits of one call never overlap, but edits of
 * separate calls may: apply them, then parse again, between calls. */
bool ui_source_set_attribute(
    const ui_source *source,
    int element,
    const char *name,
    const char *value,
    text_edit **edits,
    char *error,
    size_t error_size
);
bool ui_source_remove_attribute(
    const ui_source *source,
    int element,
    const char *name,
    text_edit **edits,
    char *error,
    size_t error_size
);
bool ui_source_add_class(
    const ui_source *source,
    int element,
    const char *name,
    text_edit **edits,
    char *error,
    size_t error_size
);
bool ui_source_remove_class(
    const ui_source *source,
    int element,
    const char *name,
    text_edit **edits,
    char *error,
    size_t error_size
);
/* Sets or, with a NULL value, removes one property of the element's style
 * attribute. */
bool ui_source_set_style(
    const ui_source *source,
    int element,
    const char *property,
    const char *value,
    text_edit **edits,
    char *error,
    size_t error_size
);
bool ui_source_insert_element(
    const ui_source *source,
    int target,
    ui_source_place place,
    const char *markup,
    text_edit **edits,
    char *error,
    size_t error_size
);
bool ui_source_move_element(
    const ui_source *source,
    int element,
    int target,
    ui_source_place place,
    text_edit **edits,
    char *error,
    size_t error_size
);
bool ui_source_delete_element(
    const ui_source *source,
    int element,
    text_edit **edits,
    char *error,
    size_t error_size
);
/* Replaces a block's whole value, adding the block when there is none. */
void ui_source_set_block(
    const ui_source *source,
    ui_source_language language,
    const char *value,
    text_edit **edits
);
void ui_source_edits_free(text_edit *edits);

#endif
