#ifndef CHIBI_BROWSER_MESSAGES_H
#define CHIBI_BROWSER_MESSAGES_H
#include "runtime/json_query.h"

#include <stdbool.h>
#include <stddef.h>
typedef struct workspace_documents workspace_documents;
typedef struct browser_messages browser_messages;
enum
{
    BROWSER_MESSAGES_QUEUE_MAX = 32,
    BROWSER_MESSAGES_TEXT_MAX = 64 * 1024
};
typedef struct
{
    bool available;
    int request, source;
    const char *kind, *text; /* Borrowed until next receive or free. */
} browser_message;
/* Root and documents may both be NULL before a workspace opens. Archive capture
 * then refuses; archive installation and transport remain available. The
 * documents service is borrowed and must outlive this service. Browser
 * callbacks own bounded opaque attachments; native builds report unavailable. */
browser_messages *browser_messages_new(
    const char *workspace_root,
    workspace_documents *,
    char *error,
    size_t
);
void browser_messages_free(browser_messages *);
/* Adapter cookie: never reused, so late callbacks cannot reach a new host. */
int browser_messages_instance(const browser_messages *);
bool browser_messages_available(const browser_messages *);
bool browser_messages_request(
    browser_messages *,
    const char *channel,
    const char *operation,
    const char *value,
    const char *json,
    const char *headers,
    int source,
    int *request,
    char *error,
    size_t
);
browser_message browser_messages_receive(browser_messages *, const char *channel);
bool browser_messages_query(
    browser_messages *,
    int source,
    const char *pointer,
    json_query_result *,
    char *error,
    size_t
);
bool browser_messages_release(browser_messages *, int source);
/* Main-thread adapter entry. Copies small metadata; opaque source is owned by
 * the adapter until released. A full queue refuses delivery and sets overflow. */
bool browser_messages_deliver(
    int instance,
    int request,
    const char *channel,
    const char *kind,
    const char *text,
    int source
);
#endif
