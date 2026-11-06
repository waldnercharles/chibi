#pragma once

void tool_init();
void tool_own_process(int pid);
void tool_release_process(int pid);
const char *tool_root();
/* How long tool_run lets a process run. */
#define TOOL_RUN_SECONDS 10

/* Runs argv from cwd with its output in logpath, stopping it after
 * TOOL_RUN_SECONDS, or `allowed` seconds for tool_run_within.
 * Returns its exit status, or 124 when it was stopped. */
int tool_run(const char *const argv[], const char *cwd, const char *logpath);
int tool_run_within(const char *const argv[], const char *cwd, const char *logpath, double allowed);
void tool_write(const char *path, const char *text);
char *tool_read(const char *path);
void tool_remove_tree(const char *path);
