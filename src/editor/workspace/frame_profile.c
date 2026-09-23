#include "editor/workspace/frame_profile.h"

#include "editor/ui/ui_shell.h"
#include "platform/cute.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const STAGE_NAMES[] = {
    "Input / window", "Documents / input routing",
    "Simulation",     "Scene submission",
    "Editor UI",      "Present / GPU wait"
};
static const ImU32 STAGE_COLORS[] = { 0xff8f8196, 0xffc19972, 0xffb8c96d,
                                      0xffe0b067, 0xffc382b7, 0xff6c8be3 };

static double milliseconds(uint64_t ticks)
{
    return 1000.0 * (double)ticks / cf_get_tick_frequency();
}

void editor_frame_begin(editor_frame_profile *p)
{
    p->current = (editor_frame_sample){ 0 };
    p->start = p->mark = cf_get_ticks();
}

void editor_frame_mark(editor_frame_profile *p, editor_frame_stage stage)
{
    assert(stage >= 0 && stage < EDITOR_FRAME_STAGE_COUNT);
    uint64_t now = cf_get_ticks();
    p->current.stages[stage] += milliseconds(now - p->mark);
    p->mark = now;
}

static int behavior_key(editor_frame_profile *p, const game_behavior_profile_row *row)
{
    for (int i = 0; i < acount(p->behavior_keys); i++) {
        const editor_behavior_key *key = &p->behavior_keys[i];
        if (!strcmp(key->behavior, row->behavior) && !strcmp(key->event, row->event)) {
            return i;
        }
    }
    if (acount(p->behavior_keys) == GAME_BEHAVIOR_PROFILE_MAX) {
        return -1;
    }
    apush(p->behavior_keys, ((editor_behavior_key){ xstrdup(row->behavior), xstrdup(row->event) }));
    return acount(p->behavior_keys) - 1;
}

static void capture_behaviors(editor_frame_profile *p, const game_behavior_profile_frame *source)
{
    editor_behavior_frame *frame = &p->behaviors[p->next];
    aclear(frame->rows);
    frame->overflow = (editor_behavior_measure){ .key = -1 };
    if (!source) {
        return;
    }
    frame->overflow.starts = source->overflow.starts;
    frame->overflow.resumes = source->overflow.resumes;
    frame->overflow.cpu_ms = source->overflow.cpu_ms;
    frame->overflow.setup_ms = source->overflow.setup_ms;
    for (int i = 0; i < source->count; i++) {
        const game_behavior_profile_row *row = &source->rows[i];
        int key = behavior_key(p, row);
        if (key < 0) {
            frame->overflow.starts += row->starts;
            frame->overflow.resumes += row->resumes;
            frame->overflow.cpu_ms += row->cpu_ms;
            frame->overflow.setup_ms += row->setup_ms;
            continue;
        }
        apush(
            frame->rows,
            ((editor_behavior_measure){ key, row->starts, row->resumes, row->cpu_ms, row->setup_ms })
        );
    }
}

void editor_frame_profile_free(editor_frame_profile *p)
{
    for (int i = 0; i < EDITOR_FRAME_HISTORY; i++) {
        afree(p->behaviors[i].rows);
    }
    for (int i = 0; i < acount(p->behavior_keys); i++) {
        free(p->behavior_keys[i].behavior);
        free(p->behavior_keys[i].event);
    }
    afree(p->behavior_keys);
    *p = (editor_frame_profile){ 0 };
}

void editor_frame_end(
    editor_frame_profile *p,
    const engine_frame_timing *runtime,
    const game_behavior_profile_frame *behaviors
)
{
    p->current.total_ms = milliseconds(cf_get_ticks() - p->start);
    p->current.runtime = *runtime;
    if (p->frozen) {
        return;
    }
    capture_behaviors(p, behaviors);
    p->samples[p->next] = p->current;
    p->next = (p->next + 1) % EDITOR_FRAME_HISTORY;
    if (p->count < EDITOR_FRAME_HISTORY) {
        p->count++;
    }
}

typedef struct
{
    const char *behavior, *event;
    uint64_t starts, resumes;
    double cpu_ms, setup_ms;
} behavior_detail;

static int compare_behavior(const void *a, const void *b)
{
    const behavior_detail *left = a, *right = b;
    if (left->cpu_ms != right->cpu_ms) {
        return (left->cpu_ms < right->cpu_ms) - (left->cpu_ms > right->cpu_ms);
    }
    int name = strcmp(left->behavior, right->behavior);
    return name ? name : strcmp(left->event, right->event);
}

static void detail_values(const behavior_detail *row, int frames)
{
    ImGui_TableNextColumn();
    ImGui_Text("%.3f", row->cpu_ms / frames);
    ImGui_TableNextColumn();
    ImGui_Text("%.3f", row->setup_ms / frames);
    ImGui_TableNextColumn();
    ImGui_Text("%llu", (unsigned long long)row->starts);
    ImGui_TableNextColumn();
    ImGui_Text("%llu", (unsigned long long)row->resumes);
}

static void draw_behaviors(const editor_frame_profile *p, const editor_frame_sample *mean)
{
    behavior_detail rows[GAME_BEHAVIOR_PROFILE_MAX] = { 0 };
    behavior_detail groups[GAME_BEHAVIOR_PROFILE_MAX] = { 0 };
    behavior_detail overflow = { .behavior = "Profile capacity", .event = "" };
    int count = acount(p->behavior_keys), group_count = 0;
    for (int i = 0; i < count; i++) {
        rows[i].behavior = p->behavior_keys[i].behavior;
        rows[i].event = p->behavior_keys[i].event;
    }
    for (int frame = 0; frame < p->count; frame++) {
        const editor_behavior_frame *
            source = &p->behaviors[(p->next + EDITOR_FRAME_HISTORY - p->count + frame) % EDITOR_FRAME_HISTORY];
        overflow.starts += source->overflow.starts;
        overflow.resumes += source->overflow.resumes;
        overflow.cpu_ms += source->overflow.cpu_ms;
        overflow.setup_ms += source->overflow.setup_ms;
        for (int i = 0; i < acount(source->rows); i++) {
            const editor_behavior_measure *value = &source->rows[i];
            rows[value->key].starts += value->starts;
            rows[value->key].resumes += value->resumes;
            rows[value->key].cpu_ms += value->cpu_ms;
            rows[value->key].setup_ms += value->setup_ms;
        }
    }
    qsort(rows, (size_t)count, sizeof *rows, compare_behavior);
    double total = overflow.cpu_ms, setup = overflow.setup_ms;
    for (int i = 0; i < count; i++) {
        if (!rows[i].starts && !rows[i].resumes && !rows[i].setup_ms) {
            continue;
        }
        total += rows[i].cpu_ms;
        setup += rows[i].setup_ms;
        int group = 0;
        while (group < group_count && strcmp(groups[group].behavior, rows[i].behavior)) {
            group++;
        }
        if (group == group_count) {
            groups[group_count++] = (behavior_detail){ .behavior = rows[i].behavior,
                                                       .event = "" };
        }
        groups[group].cpu_ms += rows[i].cpu_ms;
        groups[group].setup_ms += rows[i].setup_ms;
        groups[group].starts += rows[i].starts;
        groups[group].resumes += rows[i].resumes;
    }
    qsort(groups, (size_t)group_count, sizeof *groups, compare_behavior);
    ImGui_TextDisabled("Behavior execution");
    ImGui_Text(
        "Execution %.3f ms   Setup %.3f ms   Unassigned %.3f ms",
        total / p->count,
        setup / p->count,
        fmax(0, mean->runtime.behavior_ms - (total + setup) / p->count)
    );
    ImGui_Text(
        "Physics %.3f ms   Other systems %.3f ms",
        mean->runtime.physics_ms,
        mean->runtime.systems_ms
    );
    if (ImGui_BeginTable("behavior_profiles", 5, ImGuiTableFlags_SizingStretchProp)) {
        ImGui_TableSetupColumnEx("Behavior / event", ImGuiTableColumnFlags_WidthStretch, 1, 0);
        ImGui_TableSetupColumnEx("Exec ms", ImGuiTableColumnFlags_WidthFixed, 70, 0);
        ImGui_TableSetupColumnEx("Setup ms", ImGuiTableColumnFlags_WidthFixed, 70, 0);
        ImGui_TableSetupColumnEx("Starts", ImGuiTableColumnFlags_WidthFixed, 65, 0);
        ImGui_TableSetupColumnEx("Resumes", ImGuiTableColumnFlags_WidthFixed, 65, 0);
        ImGui_TableHeadersRow();
        for (int group = 0; group < group_count; group++) {
            ImGui_TableNextRow();
            ImGui_TableNextColumn();
            bool open = ImGui_TreeNode(groups[group].behavior);
            char path[256];
            snprintf(path, sizeof path, "performance.behavior.%s", groups[group].behavior);
            editor_ui_track_item(path);
            if (group == 0) {
                editor_ui_track_item("performance.behavior.first");
            }
            detail_values(&groups[group], p->count);
            if (!open) {
                continue;
            }
            bool first_event = true;
            for (int i = 0; i < count; i++) {
                if (strcmp(rows[i].behavior, groups[group].behavior) ||
                    (!rows[i].starts && !rows[i].resumes && !rows[i].setup_ms)) {
                    continue;
                }
                ImGui_TableNextRow();
                ImGui_TableNextColumn();
                ImGui_TextUnformatted(rows[i].event);
                snprintf(
                    path,
                    sizeof path,
                    "performance.event.%s.%s",
                    rows[i].behavior,
                    rows[i].event
                );
                editor_ui_track_item(path);
                if (group == 0 && first_event) {
                    editor_ui_track_item("performance.event.first");
                    first_event = false;
                }
                detail_values(&rows[i], p->count);
            }
            ImGui_TableSetColumnIndex(0);
            ImGui_TreePop();
        }
        if (overflow.starts || overflow.resumes || overflow.setup_ms) {
            ImGui_TableNextRow();
            ImGui_TableNextColumn();
            ImGui_TextUnformatted(overflow.behavior);
            detail_values(&overflow, p->count);
        }
        ImGui_EndTable();
    }
    ImGui_TextDisabled("%d frames", p->count);
}

const editor_frame_sample *editor_frame_latest(const editor_frame_profile *p)
{
    return p->count
             ? &p->samples[(p->next + EDITOR_FRAME_HISTORY - 1) % EDITOR_FRAME_HISTORY]
             : NULL;
}

static const editor_frame_sample *history(const editor_frame_profile *p, int index)
{
    return &p->samples[(p->next + EDITOR_FRAME_HISTORY - p->count + index) % EDITOR_FRAME_HISTORY];
}

static int compare_time(const void *left, const void *right)
{
    double a = *(const double *)left, b = *(const double *)right;
    return (a > b) - (a < b);
}

static void draw_history(const editor_frame_profile *p, double ceiling)
{
    ImVec2 min = ImGui_GetCursorScreenPos();
    ImVec2 size = { fmaxf(1, ImGui_GetContentRegionAvail().x), 136 };
    ImDrawList *draw = ImGui_GetWindowDrawList();
    ImDrawList_AddRectFilled(draw, min, (ImVec2){ min.x + size.x, min.y + size.y }, 0xff201d1b);
    float width = size.x / EDITOR_FRAME_HISTORY;
    for (int i = 0; i < p->count; i++) {
        const editor_frame_sample *sample = history(p, i);
        float x = min.x + (EDITOR_FRAME_HISTORY - p->count + i) * width;
        float y = min.y + size.y;
        for (int stage = 0; stage < EDITOR_FRAME_STAGE_COUNT; stage++) {
            float next = fmaxf(min.y, y - (float)(sample->stages[stage] / ceiling) * size.y);
            ImDrawList_AddRectFilled(draw, (ImVec2){ x, next }, (ImVec2){ x + width, y }, STAGE_COLORS[stage]);
            y = next;
        }
    }
    float budget = min.y + size.y * (1 - (float)(16.667 / ceiling));
    ImDrawList_AddLine(draw, (ImVec2){ min.x, budget }, (ImVec2){ min.x + size.x, budget }, 0xb0ffffff);
    ImGui_InvisibleButton("##frame_history", size, 0);
    editor_ui_track_item("performance.history");
    if (ImGui_IsItemHovered(0)) {
        int index = (int)((ImGui_GetIO()->MousePos.x - min.x) / width) -
                    (EDITOR_FRAME_HISTORY - p->count);
        if (index >= 0 && index < p->count) {
            const editor_frame_sample *sample = history(p, index);
            ImGui_BeginTooltip();
            ImGui_Text(
                "Frame %llu · %.2f ms",
                (unsigned long long)sample->runtime.frame,
                sample->total_ms
            );
            for (int stage = 0; stage < EDITOR_FRAME_STAGE_COUNT; stage++) {
                ImGui_Text("%s   %.2f ms", STAGE_NAMES[stage], sample->stages[stage]);
            }
            ImGui_EndTooltip();
        }
    }
}

void editor_frame_profile_draw(editor_frame_profile *p)
{
    if (!editor_ui_pane_visible(EDITOR_UI_PANE_PERFORMANCE)) {
        return;
    }
    bool open = true;
    ImGui_SetNextWindowSize((ImVec2){ 550, 570 }, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin("Performance", &open, 0)) {
        ImGui_End();
        editor_ui_set_pane_visible(EDITOR_UI_PANE_PERFORMANCE, open);
        return;
    }
    ImGui_Checkbox("Freeze capture", &p->frozen);
    editor_ui_track_item("performance.freeze");
    if (p->count) {
        bool wide = ImGui_GetContentRegionAvail().x >= 900.0f;
        if (wide) {
            ImGui_SameLine();
        }
        editor_frame_sample mean = { 0 };
        double sorted[EDITOR_FRAME_HISTORY];
        for (int i = 0; i < p->count; i++) {
            const editor_frame_sample *sample = history(p, i);
            sorted[i] = sample->total_ms;
            mean.total_ms += sample->total_ms / p->count;
            for (int stage = 0; stage < EDITOR_FRAME_STAGE_COUNT; stage++) {
                mean.stages[stage] += sample->stages[stage] / p->count;
            }
            mean.runtime.behavior_ms += sample->runtime.behavior_ms / p->count;
            mean.runtime.physics_ms += sample->runtime.physics_ms / p->count;
            mean.runtime.systems_ms += sample->runtime.systems_ms / p->count;
        }
        qsort(sorted, (size_t)p->count, sizeof *sorted, compare_time);
        ImGui_Text(
            "%.1f fps    %.2f ms average    %.2f ms p95",
            1000 / fmax(mean.total_ms, .001),
            mean.total_ms,
            sorted[(p->count - 1) * 95 / 100]
        );
        bool columns = wide &&
                       ImGui_BeginTable("performance.columns", 3, ImGuiTableFlags_SizingStretchProp);
        if (columns) {
            ImGui_TableSetupColumnEx("History", ImGuiTableColumnFlags_WidthStretch, 1.1f, 0);
            ImGui_TableSetupColumnEx("Scopes", ImGuiTableColumnFlags_WidthStretch, 0.8f, 0);
            ImGui_TableSetupColumnEx("Simulation", ImGuiTableColumnFlags_WidthStretch, 1.2f, 0);
            ImGui_TableNextColumn();
            ImGui_BeginChild("performance.history_area", (ImVec2){ 0, 0 }, 0, 0);
        }
        draw_history(p, fmax(33.333, sorted[(p->count - 1) * 95 / 100] * 1.25));
        if (columns) {
            ImGui_EndChild();
            ImGui_TableNextColumn();
            ImGui_BeginChild("performance.scopes", (ImVec2){ 0, 0 }, 0, 0);
        } else {
            ImGui_Separator();
        }
        if (ImGui_BeginTable("frame_scopes", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui_TableSetupColumnEx("Scope", ImGuiTableColumnFlags_WidthStretch, 1, 0);
            ImGui_TableSetupColumnEx("Time", ImGuiTableColumnFlags_WidthFixed, 90, 0);
            for (int stage = 0; stage < EDITOR_FRAME_STAGE_COUNT; stage++) {
                ImVec4 color = ImGui_ColorConvertU32ToFloat4(STAGE_COLORS[stage]);
                ImGui_TableNextRow();
                ImGui_TableNextColumn();
                if (stage == EDITOR_FRAME_SIMULATION) {
                    p->simulation_expanded = ImGui_TreeNodeEx(STAGE_NAMES[stage], ImGuiTreeNodeFlags_NoTreePushOnOpen);
                    editor_ui_track_item("performance.simulation");
                } else {
                    ImGui_TextColoredUnformatted(color, STAGE_NAMES[stage]);
                }
                ImGui_TableNextColumn();
                ImGui_Text("%.3f ms", mean.stages[stage]);
            }
            ImGui_EndTable();
        }
        if (columns) {
            ImGui_EndChild();
            ImGui_TableNextColumn();
            ImGui_BeginChild("performance.details", (ImVec2){ 0, 0 }, 0, 0);
        }
        if (p->simulation_expanded) {
            draw_behaviors(p, &mean);
        }
        ImGui_Separator();
        ImGui_TextDisabled("Active runtime target");
        const engine_frame_timing *last = &editor_frame_latest(p)->runtime;
        ImGui_Text(
            "%d fixed steps · %d queued events · %.3f s dropped",
            last->steps,
            last->event_backlog,
            last->dropped_seconds
        );
        if (columns) {
            ImGui_EndChild();
            ImGui_EndTable();
        }
    }
    ImGui_End();
    editor_ui_set_pane_visible(EDITOR_UI_PANE_PERFORMANCE, open);
}
