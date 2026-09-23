#ifndef CHIBI_COMMAND_TRANSPORT_H
#define CHIBI_COMMAND_TRANSPORT_H

typedef struct editor_command_request editor_command_request;
/* Decode an HTTP response body. The caller owns the returned string. */
char *editor_command_response_body(const char *response, char *error, int error_size);
editor_command_request *editor_command_request_start(const char *json);
/* Caller owns the returned JSON. NULL means pending. */
char *editor_command_request_poll(editor_command_request *request);
void editor_command_request_free(editor_command_request *request);

#endif
