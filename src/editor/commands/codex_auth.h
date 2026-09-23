#ifndef CHIBI_EDITOR_COMMANDS_CODEX_AUTH_H
#define CHIBI_EDITOR_COMMANDS_CODEX_AUTH_H

#include <stdbool.h>
#include <stddef.h>

typedef struct editor_codex_login editor_codex_login;

/* Poll returns true when login has completed; success reports its outcome. */
editor_codex_login *editor_codex_login_start(char *error, size_t error_size);
bool editor_codex_login_poll(editor_codex_login *login, bool *success, char *message, size_t message_size);
void editor_codex_login_free(editor_codex_login *login);

bool editor_codex_authenticated();
/* On success, access and account_id are heap strings owned by the caller. */
bool editor_codex_token(char **access, char **account_id, char *error, size_t error_size);
void editor_codex_sign_out();

#endif
