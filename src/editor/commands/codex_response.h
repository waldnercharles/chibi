#ifndef CHIBI_EDITOR_COMMANDS_CODEX_RESPONSE_H
#define CHIBI_EDITOR_COMMANDS_CODEX_RESPONSE_H

#include <stddef.h>

/* Convert a Codex Responses SSE stream into the provider shape consumed by
 * editor_command_model_result. The caller owns the returned JSON string. */
char *editor_codex_response_normalize(const char *sse, char *error, size_t error_size);

#endif
