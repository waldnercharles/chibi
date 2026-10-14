#ifndef CHIBI_WORKSPACE_SCAFFOLD_H
#define CHIBI_WORKSPACE_SCAFFOLD_H
#include <stdbool.h>
#include <stddef.h>
enum
{
    WORKSPACE_SCAFFOLD_FILES_MAX = 128,
    WORKSPACE_SCAFFOLD_BYTES_MAX = 16384
};
/* JSON {Name,Files:[{Path,Text}]}; validates all UTF-8 strings, portable paths,
 * distinct nonoverlapping files and a current manifest/start scene before any
 * write. Creates Parent/Name exclusively, never overwrites existing entries.
 * Failure removes only this operation's created files/directories. Foreign
 * entries introduced concurrently are preserved and reported if rollback is
 * incomplete. Parent must be an explicitly authorized directory. */
bool workspace_scaffold_create(const char *parent, const char *json, char **root, char *error, size_t);
#endif
