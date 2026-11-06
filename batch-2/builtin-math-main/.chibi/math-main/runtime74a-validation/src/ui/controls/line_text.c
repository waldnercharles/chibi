/* One line of text that changes without laying out its document: the text
 * node keeps the place its last normal layout gave it, and new glyphs are
 * set as one line from that origin. Replacing a text node makes the whole
 * document lay out again, so only a first text, overflow, or a change of
 * size uses normal layout. */
#include "ui/controls/controls.h"

#include <stdio.h>
#include <string.h>

/* Compared texts this long or longer always count as changed. */
#define LINE_TEXT 128

static bool is_text(const rml_element *el)
{
    return el && !strcmp(rml_element_tag_name(el), "#text");
}

/* The label's text node, NULL when its first child is not one. */
static rml_element *text_node(const rml_element *label)
{
    rml_element *child = rml_element_first_child(label);
    return is_text(child) ? child : NULL;
}

static bool same_text(const rml_element *node, const char *text)
{
    char current[LINE_TEXT];
    return rml_element_text(node, current, sizeof current) < sizeof current &&
           !strcmp(current, text);
}

/* A new text node holding `text`, laid out as usual. */
static void fresh_text(rml_element *label, const char *text)
{
    rml_element *node = text_node(label);
    if (node && rml_element_child_count(label) == 1 && same_text(node, text)) {
        return;
    }
    while (rml_element_child_count(label)) {
        rml_element_remove_child(label, rml_element_first_child(label));
    }
    rml_element_set_text(rml_element_create_child(label, "#text", NULL), text);
}

void line_text_set(rml_element *label, const char *text)
{
    rml_element *node = text_node(label);
    if (!node) {
        fresh_text(label, text);
        return;
    }
    int width = rml_element_string_width(node, text);
    if (width <= 0) {
        /* The first normal layout resolves the font and baseline. */
        rml_element_set_text(node, text);
        return;
    }
    rml_element_suppress_text_layout(node);
    float edges = rml_element_box_size(label, RML_BOX_BORDER).x -
                  rml_element_box_size(label, RML_BOX_CONTENT).x;
    char size[32];
    snprintf(size, sizeof size, "%gpx", width + edges);
    rml_element_set_property(label, "width", size);
    if (same_text(node, text)) {
        return;
    }
    rml_element_set_text(node, text);
    rml_element_clear_text_lines(node);
    rml_element_add_text_line(node, (rml_vec2){ 0, 0 }, text);
}

/* The text node keeps the position normal layout gave it; its new line is
 * offset from that origin so the text stays right aligned in the label's
 * content box. Overflow lays out as usual so RmlUi ellipsizes it. */
void line_text_set_right(rml_element *label, const char *text)
{
    rml_element *node = text_node(label);
    char current[LINE_TEXT];
    if (!node || rml_element_text(node, current, sizeof current) >= sizeof current) {
        fresh_text(label, text);
        return;
    }
    if (!strcmp(current, text)) {
        return;
    }
    float available = rml_element_box_size(label, RML_BOX_CONTENT).x;
    int width = rml_element_string_width(node, text);
    int old_width = rml_element_string_width(node, current);
    if (width <= 0 || old_width <= 0 || width > available || old_width > available) {
        fresh_text(label, text);
        return;
    }
    rml_vec2 origin = rml_element_absolute_offset(node, RML_BOX_CONTENT);
    rml_vec2 parent = rml_element_absolute_offset(label, RML_BOX_CONTENT);
    rml_element_suppress_text_layout(node);
    rml_element_set_text(node, text);
    rml_element_clear_text_lines(node);
    rml_element_add_text_line(
        node,
        (rml_vec2){ parent.x + available - width - origin.x, 0 },
        text
    );
}
