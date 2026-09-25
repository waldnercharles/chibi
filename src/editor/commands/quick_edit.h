#ifndef CHIBI_EDITOR_COMMANDS_QUICK_EDIT_H
#define CHIBI_EDITOR_COMMANDS_QUICK_EDIT_H

/* Return an owned, single-operation proposal for a clear edit.
 * Return NULL when the request or catalog does not support a safe decision. */
char *editor_command_quick_edit(const char *packet_json);

#endif
