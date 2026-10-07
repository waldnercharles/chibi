/* Registration and the helpers the native controls share. */
#include "ui/controls/controls_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void controls_register()
{
    number_field_register();
    color_field_register();
    splitter_register();
    live_text_register();
    choice_list_register();
    tab_list_register();
    line_chart_register();
}

rml_element *control_part(rml_element *parent, const char *tag, const char *cls)
{
    rml_element *el = rml_element_create_child(parent, tag, NULL);
    rml_element_set_class_names(el, cls);
    return el;
}

rml_element *control_icon(rml_element *parent, const char *glyph)
{
    rml_element *icon = control_part(parent, "span", "icon");
    rml_element_set_text(rml_element_create_child(icon, "#text", NULL), glyph);
    return icon;
}

void control_text(rml_element *el, const char *text)
{
    rml_element *child = rml_element_first_child(el);
    char current[128];
    bool same = child && rml_element_child_count(el) == 1 &&
                !strcmp(rml_element_tag_name(child), "#text") &&
                rml_element_text(child, current, sizeof current) < sizeof current &&
                !strcmp(current, text);
    if (same) {
        return;
    }
    while (rml_element_child_count(el)) {
        rml_element_remove_child(el, rml_element_first_child(el));
    }
    if (text[0]) {
        rml_element_set_text(rml_element_create_child(el, "#text", NULL), text);
    }
}

void control_flag(rml_element *el, const char *name, bool on)
{
    if (on == rml_element_has_attribute(el, name)) {
        return;
    }
    if (on) {
        rml_element_set_attribute(el, name, "");
        return;
    }
    rml_element_remove_attribute(el, name);
}

bool control_bool(const rml_element *el, const char *name)
{
    if (!rml_element_has_attribute(el, name)) {
        return false;
    }
    char value[8];
    rml_element_attribute(el, name, value, sizeof value);
    return strcmp(value, "false") != 0;
}

double control_number(const rml_element *el, const char *name, double fallback)
{
    if (!rml_element_has_attribute(el, name)) {
        return fallback;
    }
    char value[64];
    rml_element_attribute(el, name, value, sizeof value);
    char *end;
    double number = strtod(value, &end);
    return end == value ? fallback : number;
}

void control_string(const rml_element *el, const char *name, char *out, size_t size)
{
    out[0] = '\0';
    if (rml_element_has_attribute(el, name)) {
        rml_element_attribute(el, name, out, size);
    }
}

static void append_text(const rml_element *el, char *out, size_t size)
{
    size_t length = strlen(out);
    if (!strcmp(rml_element_tag_name(el), "#text")) {
        if (length + 1 < size) {
            rml_element_text(el, out + length, size - length);
        }
        return;
    }
    for (int i = 0; i < rml_element_child_count(el); i++) {
        append_text(rml_element_child(el, i), out, size);
    }
}

void control_text_of(const rml_element *el, char *out, size_t size)
{
    out[0] = 0;
    append_text(el, out, size);
}

float control_dp(const rml_element *el)
{
    rml_context *context = rml_element_context(el);
    float ratio = context ? rml_context_dp_ratio(context) : 1;
    return ratio > 0 ? ratio : 1;
}

bool control_within(const rml_element *el, const rml_element *ancestor)
{
    for (; el; el = rml_element_parent(el)) {
        if (el == ancestor) {
            return true;
        }
    }
    return false;
}

void control_redraw(rml_element *el)
{
    rml_context *context = rml_element_context(el);
    if (context) {
        rml_context_request_next_update(context, 0);
    }
}

void control_dispatch_number(rml_element *el, const char *type, double value, bool canceled)
{
    char text[32];
    snprintf(text, sizeof text, "%.17g", value);
    rml_element_dispatch_value(el, type, text, canceled);
}
