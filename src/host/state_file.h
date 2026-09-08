#ifndef BLUEPRINTS_HOST_STATE_FILE_H
#define BLUEPRINTS_HOST_STATE_FILE_H
#include <stdbool.h>
char *bp_read_file(const char *path);
bool bp_write_file(const char *path, const char *text);
#endif
