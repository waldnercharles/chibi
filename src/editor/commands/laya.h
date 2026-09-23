#ifndef CHIBI_EDITOR_COMMAND_LAYA_H
#define CHIBI_EDITOR_COMMAND_LAYA_H

#include <stdbool.h>
#include <stddef.h>

bool editor_command_laya_set_token(const char *token);
char *editor_command_laya_local_prepare(const char *packet);
char *editor_command_laya_prepare(const char *packet, const char *host, char *error, size_t error_size);
char *editor_command_laya_result(const char *packet, const char *response, char *error, size_t error_size);

#endif
