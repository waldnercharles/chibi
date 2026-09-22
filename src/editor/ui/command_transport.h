#ifndef CHIBI_COMMAND_TRANSPORT_H
#define CHIBI_COMMAND_TRANSPORT_H

typedef struct editor_command_request editor_command_request;
editor_command_request *editor_command_request_start(const char *json);
/* Caller owns the returned JSON. NULL means pending. */
char *editor_command_request_poll(editor_command_request *request);
void editor_command_request_free(editor_command_request *request);

#endif
