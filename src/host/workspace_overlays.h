#ifndef CHIBI_WORKSPACE_OVERLAYS_H
#define CHIBI_WORKSPACE_OVERLAYS_H
#include <stddef.h>
typedef struct workspace_documents workspace_documents;
/* Owned JSON array of named document text overlays. Refuses active gestures
 * and unnamed nonempty drafts before capture; free the result after copying. */
char *workspace_overlays_json(workspace_documents *, char *error, size_t);
#endif
