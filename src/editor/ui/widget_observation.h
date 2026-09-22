#ifndef CHIBI_WIDGET_OBSERVATION_H
#define CHIBI_WIDGET_OBSERVATION_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* ImGui's item hooks expose widgets without a second hand-maintained menu. */
    typedef struct
    {
        unsigned id;
        char label[192], window[128], role[24], value[256];
        float x, y, width, height;
        int depth;
        bool enabled, active, focused;
    } editor_widget;
    void editor_widgets_begin();
    const editor_widget *editor_widgets(int *count);
    void editor_widgets_label(unsigned id, const char *label);
    void editor_supervisor_begin(int flags, bool focus);
    void editor_widgets_focus_window(const char *name);
#ifdef __cplusplus
}
#endif
#endif
