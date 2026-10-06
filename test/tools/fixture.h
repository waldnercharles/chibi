#pragma once

void tool_init();
void tool_own_process(int pid);
void tool_release_process(int pid);
const char *tool_root();
int tool_run(const char *const argv[], const char *cwd, const char *logpath);
void tool_write(const char *path, const char *text);
char *tool_read(const char *path);
void tool_remove_tree(const char *path);
