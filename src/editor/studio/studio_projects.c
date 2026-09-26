/* The projects Studio opens: the workspace's own and those in its projects
 * directory. Opening one rebuilds the editor, so the application takes the
 * chosen root after the frame. */
#include "assets/project.h"
#include "editor/studio/studio_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_project(studio *s, const char *manifest)
{
    chibi_project project = { 0 };
    char error[256];
    if (chibi_project_open(&project, manifest, error, sizeof error) == 0) {
        apush(
            s->projects,
            ((studio_project){ .root = xstrdup(project.root),
                               .name = xstrdup(project.name) })
        );
    }
    chibi_project_free(&project);
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(((const studio_project *)a)->name, ((const studio_project *)b)->name);
}

void studio_projects_load(studio *s)
{
#ifdef __EMSCRIPTEN__
    /* Its projects open through the page (File > Import Project). */
    if (web_projects_available()) {
        return;
    }
#endif
    add_project(s, CHIBI_WORKSPACE_DIR "/" CHIBI_PROJECT_MANIFEST);
    DIR *dir = opendir(CHIBI_WORKSPACE_DIR "/projects");
    struct dirent *entry;
    while (dir && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char manifest[1024];
        snprintf(
            manifest,
            sizeof manifest,
            CHIBI_WORKSPACE_DIR "/projects/%s/" CHIBI_PROJECT_MANIFEST,
            entry->d_name
        );
        add_project(s, manifest);
    }
    if (dir) {
        closedir(dir);
    }
    qsort(s->projects, acount(s->projects), sizeof *s->projects, compare_names);
}

void studio_projects_free(studio *s)
{
    for (int i = 0; i < acount(s->projects); i++) {
        free(s->projects[i].root);
        free(s->projects[i].name);
    }
    afree(s->projects);
    free(s->open_project);
}

bool studio_project_current(const studio *s, int index)
{
    return !strcmp(s->projects[index].root, s->project_root);
}

static void request(studio *s, int index)
{
    free(s->open_project);
    s->open_project = xstrdup(s->projects[index].root);
}

void studio_open_project(studio *s, int index)
{
    if (!studio_unsaved(s)) {
        request(s, index);
        return;
    }
    kit_toast_dismiss(s->kit, s->discard_toast);
    s->discard_project = index;
    s->discard_toast = kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){ .type = KIT_TOAST_WARNING,
                           .title = "Unsaved changes",
                           .action = "Discard",
                           .duration = -1 }
    );
}

void studio_projects_frame(studio *s)
{
    if (s->discard_toast && kit_toast_action(s->kit, s->discard_toast)) {
        s->discard_toast = 0;
        request(s, s->discard_project);
    }
}

void studio_projects_menu(studio *s)
{
    kit *k = s->kit;
    if (!acount(s->projects) ||
        !kit_submenu_begin(k, "open_project", "Open Project", KIT_ICON_FOLDER)) {
        return;
    }
    for (int i = 0; i < acount(s->projects); i++) {
        char key[16];
        snprintf(key, sizeof key, "project%d", i);
        bool current = studio_project_current(s, i);
        kit_menu_item_desc d = { .label = s->projects[i].name,
                                 .checked = current,
                                 .disabled = current };
        if (kit_menu_item(k, key, &d)) {
            studio_open_project(s, i);
        }
    }
    kit_submenu_end(k);
}

char *studio_take_project(studio *s)
{
    char *root = s->open_project;
    s->open_project = NULL;
    return root;
}
