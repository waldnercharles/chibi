#pragma once

void tool_init();
void tool_own_process(int pid);
void tool_release_process(int pid);
const char *tool_root();
/* Runs argv from cwd with its output in logpath. tool_run has no time limit;
 * tool_run_within stops it after a positive finite `allowed` duration in seconds,
 * or runs without a time limit when `allowed` is zero.
 * Returns its exit status, 124 when stopped, or 2 for an invalid duration. */
int tool_run(const char *const argv[], const char *cwd, const char *logpath);
int tool_run_within(const char *const argv[], const char *cwd, const char *logpath, double allowed);
void tool_write(const char *path, const char *text);
char *tool_read(const char *path);
void tool_remove_tree(const char *path);
