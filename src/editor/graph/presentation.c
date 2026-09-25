#include "editor/ui/labels.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/math_nodes.h"
#include "support/array.h"

#include <assert.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

const char ED_ICON_WARNING[] = "\xef\x81\xb1";
const char ED_ICON_GRAPH[] = "\xef\x86\xb2";
const ImVec4 ED_NODE_BODY = { 0.16f, 0.17f, 0.19f, 1.0f };
const ImVec4 ED_NODE_BORDER = { 0.29f, 0.31f, 0.34f, 1.0f };
const editor_icon ED_GRAPH_OPEN_GLYPH = EDITOR_ICON_MD_ARROW_TOP_RIGHT;

static const ImVec4 ED_PIN_COLORS[] = {
    [PIN_FLOW] = { 1.0f, 1.0f, 1.0f, 1.0f },
    [PIN_BOOL] = { 0.95f, 0.43f, 0.48f, 1.0f },
    [PIN_I8] = { 0.34f, 0.76f, 0.65f, 1.0f },
    [PIN_U8] = { 0.34f, 0.76f, 0.65f, 1.0f },
    [PIN_I16] = { 0.013575f, 0.770f, 0.429609f, 1.0f },
    [PIN_U16] = { 0.013575f, 0.770f, 0.429609f, 1.0f },
    [PIN_I32] = { 0.013575f, 0.770f, 0.429609f, 1.0f },
    [PIN_U32] = { 0.013575f, 0.770f, 0.429609f, 1.0f },
    [PIN_I64] = { 0.413575f, 0.770f, 0.429609f, 1.0f },
    [PIN_U64] = { 0.413575f, 0.770f, 0.429609f, 1.0f },
    [PIN_F32] = { 0.65f, 0.85f, 0.39f, 1.0f },
    [PIN_F64] = { 0.65f, 0.85f, 0.39f, 1.0f },
    [PIN_NAME] = { 0.73f, 0.57f, 0.90f, 1.0f },
    [PIN_HANDLE] = { 0.36f, 0.77f, 0.85f, 1.0f },
    [PIN_STR] = { 0.91f, 0.48f, 0.73f, 1.0f },
    [PIN_STRUCT] = { 0.40f, 0.58f, 0.92f, 1.0f },
    [PIN_ENUM] = { 0.34f, 0.76f, 0.65f, 1.0f },
};

static const ImVec4 ED_NODE_TITLES[] = {
    [NODE_DISPLAY_PURE] = { 0.2f, 0.425f, 0.175f, 1.0f },
    [NODE_DISPLAY_EVENT] = { 0.8f, 0.1f, 0.1f, 1.0f },
    [NODE_DISPLAY_FUNCTION] = { 0.190525f, 0.583898f, 1.0f, 1.0f },
    [NODE_DISPLAY_FLOW] = { 0.8f, 0.4f, 0.4f, 1.0f },
    /* Unreal derives this from the variable's pin type; Phase 4 will do so too. */
    [NODE_DISPLAY_VARIABLE] = { 0.36f, 0.22f, 0.18f, 1.0f },
    [NODE_DISPLAY_LITERAL] = { 0.750f, 0.6f, 0.4f, 1.0f },
};

ImVec4 ed_pin_color(pin_type t)
{
    assert(t >= PIN_FLOW && t <= PIN_ENUM);
    assert(ED_PIN_COLORS[t].w == 1.0f);
    return ED_PIN_COLORS[t];
}

ImVec4 ed_node_title_color(const node *n)
{
    node_display_class display = n && n->desc ? n->desc->display_class
                                              : NODE_DISPLAY_PURE;
    if (display < NODE_DISPLAY_PURE || display > NODE_DISPLAY_LITERAL) {
        display = NODE_DISPLAY_PURE;
    }
    return ED_NODE_TITLES[display];
}

bool ed_is_graph_call(const node *n)
{
    return n && (n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
                 n->desc == &NODE_BehaviorExpression || n->desc == &NODE_CallMacro);
}

ed_node_presentation ed_node_appearance(const node_desc *desc)
{
    if (desc == &NODE_CallMacro) {
        return (ed_node_presentation){ NULL, EDITOR_ICON_FA_SEND };
    }
    if (desc == &NODE_CallFunction || desc == &NODE_CallPureFunction ||
        desc == &NODE_BehaviorExpression) {
        return (ed_node_presentation){ NULL, EDITOR_ICON_FA_SITEMAP };
    }
    /* Arithmetic and comparisons draw as their expression operator in every
       graph kind; reflected vector math marks itself through jit_kind. */
    const char *symbol = NULL;
    switch (desc->jit_kind) {
        case NODE_JIT_ADD: symbol = "+"; break;
        case NODE_JIT_SUB: symbol = "−"; break;
        case NODE_JIT_MUL: symbol = "×"; break;
        case NODE_JIT_DIV: symbol = "÷"; break;
        default: break;
    }
    int binding = 0;
    const char *spelling = behavior_expression_spelling(desc, &binding);
    if (spelling && binding >= 3 && binding != BEHAVIOR_EXPRESSION_PREFIX_BINDING) {
        symbol = behavior_expression_symbol(spelling);
    }
    static const char *const TRIG[][2] = { { "sin", "SIN" },
                                           { "cos", "COS" },
                                           { "tan", "TAN" } };
    for (int i = 0; spelling && !binding && i < 3; i++) {
        symbol = !strcmp(spelling, TRIG[i][0]) ? TRIG[i][1] : symbol;
    }
    if (symbol) {
        return (ed_node_presentation){ symbol, 0 };
    }
    if (desc->flow_kind == NODE_FLOW_BRANCH) {
        return (ed_node_presentation){ NULL, EDITOR_ICON_FA_CODE_FORK };
    }
    if (desc->pure_kind == NODE_PURE_READ) {
        return (ed_node_presentation){ NULL, EDITOR_ICON_FA_EYE };
    }
    if (desc->access && desc->access->write_count) {
        return (ed_node_presentation){ NULL, EDITOR_ICON_FA_PENCIL };
    }
    switch (desc->display_class) {
        case NODE_DISPLAY_EVENT:
            return (ed_node_presentation){ NULL, EDITOR_ICON_FA_BOLT };
        case NODE_DISPLAY_FLOW:
            return (ed_node_presentation){ NULL, EDITOR_ICON_FA_CODE_FORK };
        case NODE_DISPLAY_VARIABLE:
            return (ed_node_presentation){ NULL, EDITOR_ICON_FA_DATABASE };
        case NODE_DISPLAY_LITERAL:
            return (ed_node_presentation){ NULL, EDITOR_ICON_FA_CODE };
        case NODE_DISPLAY_PURE:
        case NODE_DISPLAY_FUNCTION:
            return (ed_node_presentation){ NULL, EDITOR_ICON_FA_COG };
    }
    assert(false);
    return (ed_node_presentation){ 0 };
}

const char *ed_node_prefix(const node_desc *desc, char icon[4])
{
    ed_node_presentation appearance = ed_node_appearance(desc);
    if (appearance.symbol) {
        return appearance.symbol;
    }
    unsigned glyph = (unsigned)appearance.icon;
    icon[0] = (char)(0xe0 | (glyph >> 12));
    icon[1] = (char)(0x80 | ((glyph >> 6) & 0x3f));
    icon[2] = (char)(0x80 | (glyph & 0x3f));
    icon[3] = '\0';
    return icon;
}

float ed_node_title_inset(const node *n)
{
    const char *symbol = ed_node_appearance(n->desc).symbol;
    return symbol ? ed_max(ImGui_CalcTextSize(symbol).x, 20.0f) + 8.0f : 20.0f;
}

float ed_event_status_width(const node *n)
{
    return n->desc->flow_kind == NODE_FLOW_EVENT ? 28.0f : 0.0f;
}

float ed_node_title_padding(const node *)
{
    return ED_TITLE_PAD;
}

float ed_node_body_padding(const node *)
{
    return ED_PAD;
}

/* Component nodes and the generated nodes they lower to read the same:
 * the access, then the component. */
static bool ed_component_title(const node *n, char *buf, size_t cap)
{
    node_component_kind generated = n->desc->component_kind;
    component_node_kind kind = component_node_kind_of(n->desc);
    const ecs_component_desc *component = component_node_component(n);
    if ((!kind && generated == NODE_COMPONENT_NONE) || !component) {
        return false;
    }
    bool has = kind == COMPONENT_NODE_HAS || generated == NODE_COMPONENT_HAS_SELF ||
               generated == NODE_COMPONENT_HAS_ENTITY;
    bool set = kind == COMPONENT_NODE_SET || generated == NODE_COMPONENT_SET_SELF ||
               generated == NODE_COMPONENT_SET_ENTITY ||
               generated == NODE_COMPONENT_SET_FIELD;
    char label[128];
    editor_component_label(component->name, label, sizeof label);
    snprintf(buf, cap, "%s %s", has ? "Has" : set ? "Set" : "Get", label);
    return true;
}

const char *ed_node_title(const editor *e, const node *n, char *buf, size_t cap)
{
    if (ed_component_title(n, buf, cap)) {
        return buf;
    }
    if (n->reference_name && !strcmp(n->desc->name, "Shader Parameter")) {
        snprintf(buf, cap, "%s", n->reference_name);
        return buf;
    }
    if (n->reference_name && (!strcmp(n->desc->name, "Send Event") ||
                              !strcmp(n->desc->name, "Call Event"))) {
        snprintf(buf, cap, "%s %s", !strcmp(n->desc->name, "Send Event") ? "Send" : "Call", n->reference_name);
        return buf;
    }
    if (ed_is_graph_call(n)) {
        const behavior_asset *asset = e->asset;
        if (n->reference_asset) {
            asset = NULL;
            for (int i = 0; i < acount(e->asset_catalog); i++) {
                if (!strcmp(e->asset_catalog[i]->name, n->reference_asset)) {
                    asset = e->asset_catalog[i];
                    break;
                }
            }
        }
        const graph *target = behavior_asset_find_graph(asset, n->reference_id);
        const char *name = target ? target->name : n->reference_name;
        if (n->reference_asset && target) {
            snprintf(buf, cap, "%s.%s", asset->name, name);
            return buf;
        }
        if (!name || !name[0]) {
            editor_node_label(n->desc, buf, cap);
            return buf;
        }
        if (behavior_expression_source(e->asset, n)) {
            snprintf(buf, cap, "%s", name);
        } else {
            editor_identifier_label(name, buf, cap);
        }
        return buf;
    }
    const char *expression = behavior_expression_source(e->asset, n);
    if (expression) {
        snprintf(buf, cap, "%s", expression);
        return buf;
    }
    if (n->reference_name && n->reference_name[0] &&
        n->desc->flow_kind == NODE_FLOW_EVENT) {
        snprintf(buf, cap, "On %s", n->reference_name);
        return buf;
    }
    /* A macro's tunnels read as Inputs and Outputs, as in Unreal. */
    const graph *g = e->active_graph;
    if (g && g->kind == GRAPH_MACRO && (n->id == g->entry_id || n->id == g->return_id)) {
        snprintf(buf, cap, "%s", n->id == g->entry_id ? "Inputs" : "Outputs");
        return buf;
    }
    char label[256];
    editor_node_label(n->desc, label, sizeof label);
    if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
        const behavior_variable *variable = behavior_asset_find_variable_by_id(e->asset, n->reference_id);
        if (variable && variable->name) {
            char name[192];
            editor_identifier_label(variable->name, name, sizeof name);
            snprintf(buf, cap, "%s %s", n->desc == &NODE_GetVariable ? "Get" : "Set", name);
            return buf;
        }
    }
    snprintf(buf, cap, "%s", label);
    return buf;
}

ImU32 ed_color(ImVec4 c)
{
    int r = (int)(c.x * 255.0f);
    int g = (int)(c.y * 255.0f);
    int b = (int)(c.z * 255.0f);
    int a = (int)(c.w * 255.0f);
    if (r < 0) {
        r = 0;
    }
    if (r > 255) {
        r = 255;
    }
    if (g < 0) {
        g = 0;
    }
    if (g > 255) {
        g = 255;
    }
    if (b < 0) {
        b = 0;
    }
    if (b > 255) {
        b = 255;
    }
    if (a < 0) {
        a = 0;
    }
    if (a > 255) {
        a = 255;
    }
    return IM_COL32(r, g, b, a);
}

ImU32 ed_dim_color(ImU32 c, float factor)
{
    int r = (int)((c & 255u) * factor);
    int g = (int)(((c >> 8) & 255u) * factor);
    int b = (int)(((c >> 16) & 255u) * factor);
    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }
    return IM_COL32(r, g, b, (c >> 24) & 255u);
}

ImVec4 ed_spec_color(const pin_spec *spec)
{
    return spec->kind == PIN_EXEC ? (ImVec4){ 1.0f, 0.55f, 0.20f, 1.0f }
                                  : ed_pin_color(spec->type);
}
