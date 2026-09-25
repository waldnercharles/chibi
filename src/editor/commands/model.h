#ifndef CHIBI_EDITOR_COMMAND_MODEL_H
#define CHIBI_EDITOR_COMMAND_MODEL_H

#include <stdbool.h>
#include <stddef.h>

/* Ollama hosts ending in /api use native schema output. Other hosts use the
 * OpenAI-compatible chat-completions interface. */
void editor_command_model_get_settings(char *host, size_t host_size, char *model, size_t model_size);
bool editor_command_model_set_settings(const char *host, const char *model, char *error, size_t error_size);
bool editor_command_model_json_schema();
void editor_command_model_set_json_schema(bool enabled);

/* The transport consumes this envelope and returns the provider response. */
char *editor_command_model_prepare(const char *packet, char *error, size_t error_size);
/* Convert one provider response into the editor command result. */
char *editor_command_model_result(const char *packet, const char *response, char *error, size_t error_size);

#endif
