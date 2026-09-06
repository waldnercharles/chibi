#include "flags.h"

#include "ui_shell.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <stdio.h>
#include <string.h>

unsigned editor_flags_toggle(unsigned value, unsigned flag)
{
    return (value & flag) == flag ? value & ~flag : value | flag;
}

static unsigned flags_mask(const editor_flags *flags)
{
    unsigned mask = 0;
    for (int i = 0; i < flags->count; i++) {
        assert(flags->items[i].value && flags->items[i].label && flags->items[i].label[0]);
        mask |= flags->items[i].value;
    }
    return mask;
}

void editor_flags_format(unsigned value, const editor_flags *flags, char *buf, size_t cap)
{
    if (!cap) {
        return;
    }
    buf[0] = 0;
    unsigned remaining = value;
    for (int i = 0; i < flags->count; i++) {
        const editor_flag *flag = &flags->items[i];
        assert(flag->value && flag->label && flag->label[0]);
        if ((remaining & flag->value) != flag->value) {
            continue;
        }
        size_t used = strlen(buf);
        snprintf(buf + used, cap - used, "%s%s", used ? ", " : "", flag->label);
        remaining &= ~flag->value;
    }
    if (remaining) {
        size_t used = strlen(buf);
        snprintf(buf + used, cap - used, "%s0x%X", used ? ", " : "", remaining);
    }
    if (!value) {
        snprintf(buf, cap, "None");
    }
}

bool editor_flags_draw(const char *label, unsigned *value, const editor_flags *flags)
{
    char preview[256];
    editor_flags_format(*value, flags, preview, sizeof preview);
    bool changed = false;
    bool open = ImGui_BeginCombo(label, preview, 0);
    editor_ui_track_item(strncmp(label, "##", 2) == 0 ? label + 2 : label);
    /* A single open popup owns the choice IDs. */
    if (!open) {
        return false;
    }
    unsigned known = flags_mask(flags);
    if (ImGui_SelectableEx("None", !(*value & known), 0, (ImVec2){ 0, 0 })) {
        unsigned next = *value & ~known;
        changed = next != *value;
        *value = next;
    }
    editor_ui_track_item("flags.choice.0");
    for (int i = 0; i < flags->count; i++) {
        const editor_flag *flag = &flags->items[i];
        ImGui_PushIDInt(i);
        if (ImGui_SelectableEx(flag->label, (*value & flag->value) == flag->value, 0, (ImVec2){ 0, 0 })) {
            *value = editor_flags_toggle(*value, flag->value);
            changed = true;
        }
        char id[64];
        snprintf(id, sizeof id, "flags.choice.%u", flag->value);
        editor_ui_track_item(id);
        ImGui_PopID();
    }
    ImGui_EndCombo();
    return changed;
}
