#ifndef CHIBI_SAVE_H
#define CHIBI_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct save save;

/* Each world starts with an isolated in-memory store. Hosts may bind a native
 * file (or browser localStorage key) before launching authored behaviors. */
save *save_new();
void save_free(save *save);
bool save_open(save *save, const char *path);
/* Open the explicit path when supplied, otherwise a stable project-name key.
 * Native hosts supply an existing writable directory; browsers use localStorage
 * and ignore directory. Changing the project name selects a different store. */
bool save_open_project(save *save, const char *project_name, const char *directory, const char *override);
const char *save_error(const save *save);
const char *save_get(const save *save, const char *key);
bool save_set(save *save, const char *key, const char *text);
/* Numbers share the text store, using finite JSON decimal text. A missing or
 * malformed number returns false without changing the output. */
bool save_get_number(const save *save, const char *key, double *number);
bool save_set_number(save *save, const char *key, double number);
bool save_remove(save *save, const char *key);
/* The entries in the order their keys were first set. */
int save_count(const save *save);
const char *save_key(const save *save, int index);
/* Bumped by every change to the entries, so views can refresh. */
uint64_t save_revision(const save *save);
bool save_commit(save *save);
void save_discard(save *save);
/* Owned canonical JSON of committed state; import is atomic and never writes
 * the host's storage. Used for checkpoints and isolated scenario inputs. */
char *save_export(const save *save);
bool save_import(save *save, const char *json);

/* Binary attachments commit independently of text entries. The host must bind
 * durable storage first. Keys contain 1-64 UTF-8 bytes and are encoded as a
 * single path component. Native writes replace a same-directory temporary file
 * atomically; failed writes preserve the previous attachment. Browser binary
 * storage is currently unsupported. Read returns malloc-owned bytes. */
#define SAVE_BLOB_KEY_MAX 64
#define SAVE_BLOB_BYTES_MAX (64u * 1024u * 1024u)
bool save_blob_write(save *, const char *key, const void *bytes, size_t length);
bool save_blob_read(save *, const char *key, unsigned char **bytes, size_t *length);

#endif
