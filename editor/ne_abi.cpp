// ne_abi.cpp - hand-rolled C ABI over imnodes (C++).
#include <imgui.h>
#include <imnodes.h>

#include "editor/ne_abi.h"

namespace im = IMNODES_NAMESPACE;

extern "C" void ne_spike_draw(void)
{
    static ImNodesEditorContext *ctx = im::EditorContextCreate();
    im::EditorContextSet(ctx);

    im::BeginNodeEditor();

    im::BeginNode(1);
    im::BeginNodeTitleBar();
    ImGui::TextUnformatted("const_f32");
    im::EndNodeTitleBar();
    im::BeginOutputAttribute(2);
    ImGui::TextUnformatted("value");
    im::EndOutputAttribute();
    im::EndNode();

    im::BeginNode(3);
    im::BeginNodeTitleBar();
    ImGui::TextUnformatted("add_floats");
    im::EndNodeTitleBar();
    im::BeginInputAttribute(4);
    ImGui::TextUnformatted("a");
    im::EndInputAttribute();
    im::BeginInputAttribute(5);
    ImGui::TextUnformatted("b");
    im::EndInputAttribute();
    im::BeginOutputAttribute(6);
    ImGui::TextUnformatted("sum");
    im::EndOutputAttribute();
    im::EndNode();

    im::Link(7, 2, 4);

    im::EndNodeEditor();
    im::EditorContextSet(nullptr);
}
