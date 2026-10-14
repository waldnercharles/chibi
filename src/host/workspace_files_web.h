#ifndef CHIBI_WORKSPACE_FILES_WEB_H
#define CHIBI_WORKSPACE_FILES_WEB_H
#include <stdbool.h>
#include <stddef.h>
/* Idempotent browser file/archive helpers shared by host adapters. Native no-op.
 * Module.browserWorkspaceFiles capture/install use portable {files:[{path,
 * base64,sha256}],bytes,sha256}, <=4096 files,16MiB/file,64MiB decoded/depth16;
 * canonical serialized archive <=32MiB. Installs fully validate into a fresh
 * /tmp root, never mutate an active workspace. ZIP helpers use same file policy, <=96MiB encoded input and <=1MiB names.
 * capture overlays are copied [{path,text}] and override matching disk bytes.
 * Checkpoints instead use original disk bytes and optional sorted attachments
 * {name,base64,bytes,sha256}; SHA covers {files,attachments}, and total bytes
 * includes both. <=8 attachments,32MiB each,64MiB retained metadata across
 * <=8 pending roots. Cancellation/adoption frees only matching root metadata. */
void workspace_files_web_init();
/* Protect a root the host successfully adopted from later staging cleanup. */
void workspace_files_web_adopt(const char *root);
/* Release only a previously installed/adopted root after its consumers retire.
 * A retained root within the same installation protects that installation;
 * foreign roots are never removed. Returns one only when removal completes. */
bool workspace_files_web_release(const char *retired, const char *retained);
/* Owned copy of an installed root's optional document-state attachment.
 * NULL/zero means absent. The caller frees the result. Adoption releases the
 * root's retained metadata only after the candidate host has restored it. */
unsigned char *workspace_files_web_document_state(const char *root, size_t *bytes);
#endif
