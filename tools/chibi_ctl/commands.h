#ifndef CHIBI_CTL_COMMANDS_H
#define CHIBI_CTL_COMMANDS_H
#include <stddef.h>
/* Convert direct verb arguments to an owned automation request. */
char *chibi_ctl_command(int argc, char **argv, char *error, size_t size);
#endif
