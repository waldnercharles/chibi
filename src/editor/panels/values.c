#include "editor/graph/eval_view.h"
#include "editor/ui/color.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "runtime/generated.h"

#include <assert.h>
#include <ctype.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

float ed_font_size(const editor *e)
{
    return ImGui_GetFontSize() * e->zoom;
}

ImVec2 ed_text_size(const editor *e, const char *text)
{
    return ed_scale(ImGui_CalcTextSize(text), e->zoom);
}

void ed_draw_text(const editor *e, ImDrawList *dl, ImVec2 pos, ImU32 color, const char *text)
{
    float size = ed_font_size(e);
    ImDrawList_AddTextImFontPtr(
        dl,
        ImGui_GetFont(),
        size,
        ed_add(pos, (ImVec2){ 1.0f, 1.0f }),
        IM_COL32(0, 0, 0, 210),
        text
    );
    ImDrawList_AddTextImFontPtr(dl, ImGui_GetFont(), size, pos, color, text);
}

static const pin_value *ed_pin_authored(const node *n, int kind, int index)
{
    return kind == ED_PIN_INPUT ? &n->input_pins[index].default_value
                                : &n->output_pins[index].default_value;
}

static int ed_edit_hit(const editor *e, ImVec2 mouse, ImVec2 pos, float width)
{
    return ed_point_in_rect(
        mouse,
        pos,
        (ImVec2){ pos.x + width, pos.y + ImGui_GetFrameHeight() * e->zoom }
    );
}

static const editor_flags *ed_pin_flags(editor *e, node *n, int output, int index)
{
    if (output || !e->flags_resolver) {
        return NULL;
    }
    const pin_spec *spec = &n->input_pins[index];
    if (spec->type != PIN_U32 || spec->container != PIN_CONTAINER_SINGLE) {
        return NULL;
    }
    return e->flags_resolver(n, output, index, e->flags_user);
}

static const editor_choices *ed_pin_choices(editor *e, node *n, int output, int index)
{
    if (output || !e->choices_resolver) {
        return NULL;
    }
    const pin_spec *spec = &n->input_pins[index];
    if ((spec->type != PIN_I32 && spec->type != PIN_NAME) ||
        spec->container != PIN_CONTAINER_SINGLE) {
        return NULL;
    }
    return e->choices_resolver(n, output, index, e->choices_user);
}

static bool ed_draw_choice_input(node *n, int index, pin_value *value, const editor_choices *choices, bool details)
{
    char id[96];
    snprintf(
        id,
        sizeof id,
        "choices.%s%llu.0.%d",
        details ? "details." : "",
        (unsigned long long)n->id,
        index
    );
    return value->type == PIN_NAME
             ? editor_choices_draw_name(id, &value->str, choices)
             : editor_choices_draw(id, &value->i, choices);
}

static bool ed_pin_color_input(editor *e, node *n, int output, int index)
{
    if (output || !e->color_resolver) {
        return false;
    }
    const pin_spec *spec = &n->input_pins[index];
    return spec->type == PIN_U32 && spec->container == PIN_CONTAINER_SINGLE &&
           e->color_resolver(n, output, index, e->color_user);
}

static bool ed_draw_color_input(editor *e, node *n, int index, unsigned *value, bool details)
{
    char id[96];
    snprintf(
        id,
        sizeof id,
        "color.%s%llu.0.%d",
        details ? "details." : "",
        (unsigned long long)n->id,
        index
    );
    bool first_change;
    bool changed = editor_color_draw(&e->color_state, id, value, &first_change);
    if (first_change) {
        ed_snapshot_before(e);
    }
    return changed;
}

static bool float_color(const pin_spec *pin)
{
    return (pin->flags & PIN_FLAG_COLOR) && ed_vector_width(pin) >= 3;
}

static bool ed_draw_vector_input(editor *e, node *n, int output, int index, float value[4], bool details)
{
    const pin_spec *pin = output ? &n->output_pins[index] : &n->input_pins[index];
    int width = ed_vector_width(pin);
    assert(width);
    char id[96];
    snprintf(
        id,
        sizeof id,
        "vector.%s%llu.%d.%d",
        details ? "details." : "",
        (unsigned long long)n->id,
        output,
        index
    );
    if (float_color(pin)) {
        bool first;
        bool changed = editor_color_draw_float(&e->color_state, id, value, width, &first);
        if (first) {
            ed_snapshot_before(e);
        }
        return changed;
    }
    char label[100];
    snprintf(label, sizeof label, "##%s", id);
    bool changed = ImGui_InputScalarNEx(label, ImGuiDataType_Float, value, width, NULL, NULL, "%.4g", 0);
    editor_ui_track_item(id);
    return changed;
}

static void ed_commit_discrete_value(editor *e, node *n, int index, const pin_value *value)
{
    /* Popup selections are discrete edits, independent of item deactivation. */
    ed_snapshot_before(e);
    int result = graph_set_pin_default(e->active_graph, n, 0, index, value);
    assert(result == 0);
    (void)result;
}

static const char *input_context(const node *n, int output, int index)
{
    const char *label = NULL;
    if (!output) {
        shader_node_input_context(n, index, &label);
    }
    return label;
}

static bool draw_context_choice(editor *e, node *n, int index, const char *context)
{
    pin_spec *pin = &n->input_pins[index];
    bool has_value = pin->default_value.ptr != NULL;
    char id[96];
    snprintf(
        id,
        sizeof id,
        "shader.context.details.%llu.%d",
        (unsigned long long)n->id,
        index
    );
    ImGui_SetNextItemWidth(ImGui_GetContentRegionAvail().x);
    bool open = ImGui_BeginCombo("##context", has_value ? "Value" : context, 0);
    editor_ui_track_item(id);
    if (!open) {
        return false;
    }
    bool changed = false;
    const char *labels[] = { context, "Value" };
    for (int i = 0; i < 2; i++) {
        bool chosen = ImGui_SelectableEx(labels[i], i == has_value, 0, (ImVec2){ 0 });
        char choice[112];
        snprintf(choice, sizeof choice, "%s.%s", id, i ? "value" : "context");
        editor_ui_track_item(choice);
        if (chosen && i != has_value) {
            float zero[4] = { 0 };
            pin_value value = { .type = pin->type,
                                .type_name = pin->type_name,
                                .ptr = i ? zero : NULL };
            ed_commit_discrete_value(e, n, index, &value);
            changed = true;
        }
    }
    ImGui_EndCombo();
    return changed;
}

void ed_begin_value_edit(editor *e, node *n, int kind, int index, ImVec2 pos)
{
    const pin_spec *pin = kind == ED_PIN_OUTPUT ? &n->output_pins[index]
                                                : &n->input_pins[index];
    if ((input_context(n, kind == ED_PIN_OUTPUT, index) && !pin->default_value.ptr) ||
        ed_pin_choices(e, n, kind == ED_PIN_OUTPUT, index) ||
        ed_pin_flags(e, n, kind == ED_PIN_OUTPUT, index) ||
        ed_pin_color_input(e, n, kind == ED_PIN_OUTPUT, index) || float_color(pin)) {
        return;
    }
    ImGuiIO *io = ImGui_GetIO();
    if (!e->edit_history_open && ImGui_IsMouseClicked(0) &&
        ed_edit_hit(e, io->MousePos, pos, ed_pin_value_width(e->active_graph, n, kind, index) * e->zoom)) {
        e->pending_snapshot = ed_snapshot(e);
        e->edit_history_open = 1;
        e->edit_changed = 0;
    }
}

static void ed_finish_value_edit(editor *e, int changed)
{
    if (changed) {
        e->edit_changed = 1;
    }
    if (e->edit_history_open && ImGui_IsItemDeactivated()) {
        if (e->edit_changed) {
            ed_commit_snapshot(e);
        } else {
            ed_cancel_snapshot(e);
        }
        e->edit_history_open = 0;
        e->edit_changed = 0;
    }
}

int ed_draw_value_editor(editor *e, node *n, int kind, int index, ImVec2 pos)
{
    const pin_spec *bound_spec = kind == ED_PIN_OUTPUT ? &n->output_pins[index]
                                                       : &n->input_pins[index];
    if (kind == ED_PIN_INPUT &&
        (bound_spec->binding || ed_inline_source_for(e, n, index, NULL, NULL))) {
        return ed_draw_bound_input(
            e,
            n,
            index,
            pos,
            ed_pin_value_width(e->active_graph, n, kind, index) * e->zoom
        );
    }
    pin_value authored = *ed_pin_authored(n, kind, index);
    pin_value *v = &authored;
    const editor_choices *choices = ed_pin_choices(e, n, kind == ED_PIN_OUTPUT, index);
    const editor_flags *flags = ed_pin_flags(e, n, kind == ED_PIN_OUTPUT, index);
    bool color = ed_pin_color_input(e, n, kind == ED_PIN_OUTPUT, index);
    const pin_spec *spec = bound_spec;
    float vector[4] = { 0 };
    int changed = 0;
    char id[32];
    snprintf(id, sizeof id, "##value_%d_%d", kind, index);
    float width = ed_pin_value_width(e->active_graph, n, kind, index) * e->zoom;
    bool binding_trigger = kind == ED_PIN_INPUT &&
                           ed_binding_trigger_available(e, n, index);
    float binding_size = ImGui_GetFrameHeight() * e->zoom;
    float control_width = binding_trigger ? width - binding_size - 4.0f * e->zoom
                                          : width;
    if (control_width < 32.0f * e->zoom) {
        control_width = width;
        binding_trigger = false;
    }
    ImGui_SetCursorScreenPos(pos);
    ImGui_PushFontFloat(NULL, ImGui_GetFontSize() * e->zoom);
    ImGui_SetNextItemWidth(control_width);
    ImGui_PushStyleVarImVec2(
        ImGuiStyleVar_FramePadding,
        ed_scale(ImGui_GetStyle()->FramePadding, e->zoom)
    );
    ImGui_PushIDPtr(n);
    ImGui_PushIDInt(kind * 1000 + index);
    switch (v->type) {
        case PIN_BOOL: changed = ImGui_Checkbox(id, &v->b); break;
        case PIN_I8:
            changed = ImGui_InputScalar(id, ImGuiDataType_S8, &v->i8);
            break;
        case PIN_U8:
            changed = ImGui_InputScalar(id, ImGuiDataType_U8, &v->u8);
            break;
        case PIN_I16:
            changed = ImGui_InputScalar(id, ImGuiDataType_S16, &v->i16);
            break;
        case PIN_U16:
            changed = ImGui_InputScalar(id, ImGuiDataType_U16, &v->u16);
            break;
        case PIN_I32:
            if (choices) {
                changed = ed_draw_choice_input(n, index, v, choices, false);
                break;
            }
            changed = ImGui_InputScalar(id, ImGuiDataType_S32, &v->i);
            break;
        case PIN_ENUM:
            changed = ImGui_InputScalar(id, ImGuiDataType_S32, &v->e);
            break;
        case PIN_U32:
            if (color) {
                changed = ed_draw_color_input(e, n, index, &v->u, false);
                break;
            }
            if (flags) {
                char flag_id[96];
                snprintf(
                    flag_id,
                    sizeof flag_id,
                    "##flags.%llu.0.%d",
                    (unsigned long long)n->id,
                    index
                );
                changed = editor_flags_draw(flag_id, &v->u, flags);
                break;
            }
            changed = ImGui_InputScalar(id, ImGuiDataType_U32, &v->u);
            break;
        case PIN_I64:
            changed = ImGui_InputScalar(id, ImGuiDataType_S64, &v->i64);
            break;
        case PIN_U64:
            changed = ImGui_InputScalar(id, ImGuiDataType_U64, &v->u64);
            break;
        case PIN_F32:
            changed = ImGui_InputFloatEx(id, &v->f, 0.0f, 0.0f, "%.6g", 0);
            break;
        case PIN_F64:
            changed = ImGui_InputDoubleEx(id, &v->d, 0.0, 0.0, "%.12g", 0);
            break;
        case PIN_STR: {
            if (e->edit_text_node != n || e->edit_text_kind != kind ||
                e->edit_text_index != index) {
                snprintf(e->edit_text, sizeof e->edit_text, "%s", v->str ? v->str : "");
                e->edit_text_node = n;
                e->edit_text_kind = kind;
                e->edit_text_index = index;
            }
            changed = ImGui_InputText(id, e->edit_text, sizeof e->edit_text, 0);
            if (changed) {
                char *copy = graph_own_str(e->active_graph, e->edit_text, strlen(e->edit_text));
                if (copy) {
                    v->str = copy;
                }
            }
            if (!ImGui_IsItemActive()) {
                e->edit_text_node = NULL;
            }
            break;
        }
        case PIN_NAME: {
            if (choices) {
                changed = ed_draw_choice_input(n, index, v, choices, false);
                break;
            }
            if (e->edit_text_node != n || e->edit_text_kind != kind ||
                e->edit_text_index != index) {
                snprintf(e->edit_text, sizeof e->edit_text, "%s", v->str ? v->str : "");
                e->edit_text_node = n;
                e->edit_text_kind = kind;
                e->edit_text_index = index;
            }
            changed = ImGui_InputText(id, e->edit_text, sizeof e->edit_text, 0);
            if (changed) {
                char *copy = graph_own_str(e->active_graph, e->edit_text, strlen(e->edit_text));
                if (copy) {
                    v->str = copy;
                }
            }
            if (!ImGui_IsItemActive()) {
                e->edit_text_node = NULL;
            }
            break;
        }
        case PIN_STRUCT:
            if (ed_vector_width(spec)) {
                const char *context = input_context(n, kind == ED_PIN_OUTPUT, index);
                if (!v->ptr && context) {
                    ImGui_AlignTextToFramePadding();
                    ImGui_TextDisabled("%s", context);
                    char context_id[96];
                    snprintf(
                        context_id,
                        sizeof context_id,
                        "shader.context.%llu.%d",
                        (unsigned long long)n->id,
                        index
                    );
                    editor_ui_track_item(context_id);
                    break;
                }
                if (v->ptr) {
                    memcpy(vector, v->ptr, (size_t)ed_vector_width(spec) * sizeof(float));
                }
                changed = ed_draw_vector_input(e, n, kind == ED_PIN_OUTPUT, index, vector, false);
                v->ptr = vector;
            }
            break;
        case PIN_FLOW:
        default: break;
    }
    ImGui_PopID();
    ImGui_PopID();
    ImGui_PopStyleVar();
    ImGui_PopFont();
    bool binding_clicked = binding_trigger &&
                           ed_draw_binding_trigger(
                               e,
                               n,
                               index,
                               (ImVec2){ pos.x + control_width + 4.0f * e->zoom, pos.y },
                               binding_size
                           );
    if (color || float_color(spec)) {
        if (changed) {
            graph_set_pin_default(e->active_graph, n, kind == ED_PIN_OUTPUT, index, v);
        }
        (void)binding_clicked;
        return changed;
    }
    if (flags || choices) {
        if (changed) {
            ed_commit_discrete_value(e, n, index, v);
        }
        (void)binding_clicked;
        return changed;
    }
    if (changed) {
        graph_set_pin_default(e->active_graph, n, kind == ED_PIN_OUTPUT, index, v);
    }
    ed_finish_value_edit(e, changed);
    (void)binding_clicked;
    return changed;
}

int ed_pin_type_combo(const char *label, pin_type *type)
{
    int selected = *type;
    const char *items = "Exec\0Bool\0Int8\0Byte\0Int16\0UInt16\0Integer\0"
                        "UInt32\0Integer64\0UInt64\0Real (float)\0Real "
                        "(double)\0Name\0String\0Handle\0Struct\0Enum\0\0";
    if (!ImGui_Combo(label, &selected, items)) {
        return 0;
    }
    *type = (pin_type)selected;
    return 1;
}

int ed_pin_container_combo(const char *label, pin_container *container)
{
    int selected = *container;
    const char *items = "Single\0Array\0Set\0Map\0\0";
    if (!ImGui_Combo(label, &selected, items)) {
        return 0;
    }
    *container = (pin_container)selected;
    return 1;
}

void ed_details_begin_mutation(editor *e)
{
    if (e->details_history_open) {
        return;
    }
    e->pending_snapshot = ed_snapshot(e);
    e->details_history_open = 1;
}

void ed_details_finish_mutation(editor *e, int changed)
{
    if (changed) {
        e->edit_changed = 1;
    }
    if (e->details_history_open && ImGui_IsItemDeactivated()) {
        if (e->edit_changed) {
            ed_commit_snapshot(e);
        } else {
            ed_cancel_snapshot(e);
        }
        e->details_history_open = 0;
        e->edit_changed = 0;
    }
}

static int ed_draw_details_default(editor *e, node *n, int output, int index)
{
    pin_spec *spec = output ? &n->output_pins[index] : &n->input_pins[index];
    if (spec->kind != PIN_DATA) {
        ImGui_TextUnformatted("(exec)");
        return 0;
    }
    const char *context = input_context(n, output, index);
    bool context_changed = context && draw_context_choice(e, n, index, context);
    if (context && !spec->default_value.ptr) {
        return context_changed;
    }
    pin_value value = spec->default_value;
    const editor_choices *choices = ed_pin_choices(e, n, output, index);
    const editor_flags *flags = ed_pin_flags(e, n, output, index);
    bool color = ed_pin_color_input(e, n, output, index);
    float vector[4] = { 0 };
    int changed = 0;
    char id[32];
    snprintf(id, sizeof id, "##Value%d", index);
    if (value.type != PIN_STR) {
        ImGui_SetNextItemWidth(ImGui_GetContentRegionAvail().x);
    }
    switch (value.type) {
        case PIN_BOOL: changed = ImGui_Checkbox(id, &value.b); break;
        case PIN_I8:
            changed = ImGui_InputScalar(id, ImGuiDataType_S8, &value.i8);
            break;
        case PIN_U8:
            changed = ImGui_InputScalar(id, ImGuiDataType_U8, &value.u8);
            break;
        case PIN_I16:
            changed = ImGui_InputScalar(id, ImGuiDataType_S16, &value.i16);
            break;
        case PIN_U16:
            changed = ImGui_InputScalar(id, ImGuiDataType_U16, &value.u16);
            break;
        case PIN_I32:
            changed = choices ? ed_draw_choice_input(n, index, &value, choices, true)
                              : ImGui_InputInt(id, &value.i);
            break;
        case PIN_U32:
            if (color) {
                changed = ed_draw_color_input(e, n, index, &value.u, true);
                break;
            }
            if (flags) {
                char flag_id[96];
                snprintf(
                    flag_id,
                    sizeof flag_id,
                    "##flags.details.%llu.0.%d",
                    (unsigned long long)n->id,
                    index
                );
                changed = editor_flags_draw(flag_id, &value.u, flags);
                break;
            }
            changed = ImGui_InputScalar(id, ImGuiDataType_U32, &value.u);
            break;
        case PIN_I64:
            changed = ImGui_InputScalar(id, ImGuiDataType_S64, &value.i64);
            break;
        case PIN_U64:
            changed = ImGui_InputScalar(id, ImGuiDataType_U64, &value.u64);
            break;
        case PIN_F32:
            changed = ImGui_InputFloatEx(id, &value.f, 0.1f, 0.0f, "%.6g", 0);
            break;
        case PIN_F64:
            changed = ImGui_InputDoubleEx(id, &value.d, 0.1, 0.0, "%.12g", 0);
            break;
        case PIN_ENUM: changed = ImGui_InputInt(id, &value.e); break;
        case PIN_STR: {
            snprintf(e->edit_text, sizeof e->edit_text, "%s", value.str ? value.str : "");
            changed = ImGui_InputTextMultilineEx(
                id,
                e->edit_text,
                sizeof e->edit_text,
                (ImVec2){ 0.0f, 0.0f },
                ImGuiInputTextFlags_WordWrap,
                NULL,
                NULL
            );
            value.str = e->edit_text;
            break;
        }
        case PIN_NAME: {
            if (choices) {
                changed = ed_draw_choice_input(n, index, &value, choices, true);
                break;
            }
            snprintf(e->edit_text, sizeof e->edit_text, "%s", value.str ? value.str : "");
            changed = ImGui_InputText(id, e->edit_text, sizeof e->edit_text, 0);
            value.str = e->edit_text;
            break;
        }
        case PIN_STRUCT:
            if (ed_vector_width(spec)) {
                if (value.ptr) {
                    memcpy(vector, value.ptr, (size_t)ed_vector_width(spec) * sizeof(float));
                }
                changed = ed_draw_vector_input(e, n, output, index, vector, true);
                value.ptr = vector;
                break;
            }
            [[fallthrough]];
        default: {
            const char *text = output
                                 ? eval_view_output(e->ev, n, index)
                                 : eval_view_input(e->ev, e->active_graph, n, index);
            ImGui_TextDisabled("%s", text && text[0] ? text : "Connect a value");
            break;
        }
    }
    if (color || float_color(spec)) {
        if (changed) {
            graph_set_pin_default(e->active_graph, n, output, index, &value);
        }
        return changed;
    }
    if (flags || choices) {
        if (changed) {
            ed_commit_discrete_value(e, n, index, &value);
        }
        return changed;
    }
    if (changed) {
        ed_details_begin_mutation(e);
        if (graph_set_pin_default(e->active_graph, n, output, index, &value) == 0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, changed);
    return changed;
}

void ed_draw_details_pin(editor *e, node *n, int output, int index)
{
    pin_spec *spec = output ? &n->output_pins[index] : &n->input_pins[index];
    if (spec->kind == PIN_EXEC || (spec->flags & PIN_FLAG_SETTING)) {
        return;
    }
    char pin_label[256];
    ed_pin_label(n, spec, pin_label, sizeof pin_label);
    ImGui_PushIDInt((output ? 10000 : 0) + index);
    ImGui_Spacing();
    ImGui_TextUnformatted(pin_label);
    char id[96];
    snprintf(
        id,
        sizeof id,
        "inspector.%s.%llu.%d",
        output ? "output" : "input",
        (unsigned long long)n->id,
        index
    );
    editor_ui_track_item(id);
    node *source = NULL;
    int source_pin = 0;
    if (!output && graph_input_source(e->active_graph, n, index, &source, &source_pin)) {
        char title[256];
        char field[256];
        ed_pin_label(source, &source->output_pins[source_pin], field, sizeof field);
        ImGui_TextWrapped("From %s · %s", ed_node_title(e, source, title, sizeof title), field);
        snprintf(
            id,
            sizeof id,
            "inspector.connection.%llu.%d",
            (unsigned long long)n->id,
            index
        );
        editor_ui_track_item(id);
        const char *value = eval_view_output(e->ev, source, source_pin);
        if (value && value[0]) {
            ImGui_TextDisabled("%s", value);
        }
        if (spec->binding) {
            char dormant[256];
            ImGui_TextDisabled(
                "Dormant source: %s",
                ed_binding_label(e, spec->binding, dormant, sizeof dormant)
            );
            editor_ui_track_item("inspector.input.binding.dormant");
        }
        if (ed_inline_source_for(e, n, index, NULL, NULL)) {
            ImVec2 action_size = { ImGui_GetContentRegionAvail().x, 0 };
            if (ImGui_ButtonEx("Change source", action_size)) {
                ed_open_binding_popup(e, n, index);
            }
            editor_ui_track_item("inspector.input.source.change");
            if (ImGui_ButtonEx("Expand source", action_size)) {
                ed_expand_inline_source(e, n, index);
            }
            editor_ui_track_item("inspector.input.source.expand");
            if (ImGui_ButtonEx("Use literal", action_size)) {
                ed_clear_input_source(e, n, index);
            }
            editor_ui_track_item("inspector.input.source.literal");
        }
        ImGui_PopID();
        return;
    }
    if (!output && spec->binding) {
        char source_label[256];
        ImGui_TextWrapped("Source: %s", ed_binding_label(e, spec->binding, source_label, sizeof source_label));
        editor_ui_track_item("inspector.input.binding.source");
        ImGui_SetItemTooltip("Literal fallback remains available while this source is active.");
        e->binding_node = n;
        e->binding_node_id = n->id;
        e->binding_pin = index;
        ImVec2 action_size = { ImGui_GetContentRegionAvail().x, 0 };
        if (ImGui_ButtonEx("Change", action_size)) {
            ed_open_binding_popup(e, n, index);
        }
        editor_ui_track_item("inspector.input.binding.change");
        bool can_jump = ed_binding_can_jump(e);
        ImGui_BeginDisabled(!can_jump);
        if (ImGui_ButtonEx("Jump", action_size)) {
            ed_jump_binding_source(e);
        }
        ImGui_EndDisabled();
        editor_ui_track_item("inspector.input.binding.jump");
        bool can_convert = ed_binding_can_convert(e);
        ImGui_BeginDisabled(!can_convert);
        if (ImGui_ButtonEx("Convert to getter", action_size)) {
            if (ed_convert_binding(e) == 0) {
                e->binding_node = NULL;
                e->binding_node_id = 0;
                e->binding_pin = -1;
            }
        }
        ImGui_EndDisabled();
        editor_ui_track_item("inspector.input.binding.convert");
        bool clear = ImGui_ButtonEx("Clear", action_size);
        editor_ui_track_item("inspector.input.binding.clear");
        if (clear) {
            ed_snapshot_before(e);
            int result = graph_set_pin_binding(e->active_graph, n, 0, index, NULL);
            assert(result == 0);
            (void)result;
            e->binding_node = NULL;
            e->binding_node_id = 0;
            e->binding_pin = -1;
        }
        editor_ui_track_item("inspector.input.binding");
        ImGui_PopID();
        return;
    }
    if (output && !ed_pin_editable(e->active_graph, n, ED_PIN_OUTPUT, index)) {
        const char *value = eval_view_output(e->ev, n, index);
        if (value && value[0]) {
            ImGui_TextDisabled("%s", value);
        }
        ImGui_PopID();
        return;
    }
    if (!spec->field_source) {
        if (!output && ed_source_scope_available(e) &&
            ImGui_ButtonEx("Bind...", (ImVec2){ 0, 0 })) {
            ed_open_binding_popup(e, n, index);
        }
        if (!output && ed_source_scope_available(e)) {
            editor_ui_track_item("inspector.input.binding.open");
        }
        ed_draw_details_default(e, n, output, index);
    }
    ImGui_PopID();
}

/* UE's Details panel treats a wire as a first-class selection.  Keep this
 * deliberately read-only: the canvas remains the place where topology is
 * edited, while the panel answers the useful "what is this connection?"
 * question without making the user hunt for either endpoint. */
static void ed_draw_details_edge_endpoint(editor *e, const char *label, uint64_t node_id, uint64_t pin_id)
{
    node *n = graph_node_by_id(e->active_graph, node_id);
    int output = 0;
    int pin_index = -1;
    const pin_spec *pin = n ? graph_pin_by_id(n, pin_id, &output, &pin_index) : NULL;
    char title[256];
    const char *node_name = n ? ed_node_title(e, n, title, sizeof title) : "Missing node";
    const char *pin_name = pin && pin->name ? pin->name : "Missing pin";
    ImGui_Text("%s", label);
    ImGui_SameLine();
    ImGui_Text("%s.%s", node_name, pin_name);
    if (pin) {
        ImGui_Text(
            "    %s | %s | %s",
            output ? "Output" : "Input",
            pin->kind == PIN_EXEC ? "Exec" : "Data",
            pin->kind == PIN_EXEC ? "flow" : pin_type_name(pin->type)
        );
    }
    ImGui_SameLine();
    ImGui_PushIDInt((int)(node_id ^ pin_id));
    if (ImGui_SmallButton("Focus")) {
        editor_reveal_node(e, n);
    }
    ImGui_PopID();
}

void ed_draw_details_edge(editor *e, const ed_link_ref *ref)
{
    int edge_index = ed_edge_index(e, ref);
    if (edge_index < 0) {
        ImGui_TextUnformatted("Connection no longer exists");
        return;
    }
    const edge *ed = &e->active_graph->edges[edge_index];
    ImGui_TextUnformatted("Connection");
    ImGui_Text("Kind: %s", graph_edge_kind_name(ed->kind));
    ed_draw_details_edge_endpoint(e, "From:", ed->from_id, ed->from_pin_id);
    ed_draw_details_edge_endpoint(e, "To:", ed->to_id, ed->to_pin_id);
    if (ed->kind == GRAPH_EDGE_EXEC) {
        ImGui_Text("Execution order: %d", ed->exec_order);
    } else if (ed->conversion) {
        ImGui_Text("Conversion: %s", ed->conversion->name);
    } else {
        ImGui_TextUnformatted("Conversion: none");
    }
    ImGui_Text("Reroute points: %d", ed->reroute_count);
    const flow_traversal *record = ed_latest_flow_record(e, ed);
    ImGui_SeparatorText("Runtime");
    if (record) {
        ImGui_Text(
            "Last traversed: %.3fs ago",
            ed_max(0.0f, (float)(e->monotonic_time - record->timestamp))
        );
        ImGui_Text("Activation: %lu", record->activation);
    } else {
        ImGui_TextUnformatted("Not traversed in the current run");
    }
}
