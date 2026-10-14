#include "host/browser_messages.h"

#include "authoring/workspace_documents.h"
#include "host/workspace_files_web.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
// clang-format off
EM_JS(int, web_available, (), { return !!Module['browserMessages']; })
EM_JS(
    int,
    browser_messages_web_request,
    (int instance,
     int request,
     const char *channel,
     const char *operation,
     const char *value,
     const char *json,
     const char *headers,
     int source,
     const char *root,
     const unsigned char *snapshot,
     size_t snapshot_bytes),
    {
        return Module['browserMessages'] ?.request(
                                              instance,
                                              request,
                                              UTF8ToString(channel),
                                              UTF8ToString(operation),
                                              UTF8ToString(value),
                                              UTF8ToString(json),
                                              UTF8ToString(headers),
                                              source,
                                              UTF8ToString(root),
                                              snapshot_bytes ? HEAPU8.subarray(snapshot,snapshot+snapshot_bytes) : null
                                          )
                                            ? 1
                                            : 0;
    }
)
EM_JS(char *, web_text, (int instance, int source), {
    const text = Module['browserMessages'] ?.text(instance, source);
    return typeof text === 'string' ? stringToNewUTF8(text) : 0;
})
EM_JS(int, web_release, (int instance, int source), {
    return Module['browserMessages'] ?.release(instance, source) ? 1 : 0;
})
EM_JS(void, web_destroy, (int instance), {
    Module['browserMessages'] ?.destroy(instance);
})
// clang-format on
#else
#define EXPORT
#endif

typedef struct
{
    int request, source;
    char *channel, *kind, *text;
} queued_message;
struct browser_messages
{
    int instance, next_request, count;
    char *root;
    workspace_documents *documents;
    queued_message queue[BROWSER_MESSAGES_QUEUE_MAX], current;
    bool overflow;
};
static browser_messages *instances[8];
static int next_instance = 1;
static bool token(const char *text)
{
    if (!text || !*text || strnlen(text, 65) > 64) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) {
            return false;
        }
    }
    return true;
}
static void clear(queued_message *message)
{
    free(message->channel);
    free(message->kind);
    free(message->text);
    *message = (queued_message){ 0 };
}
browser_messages *browser_messages_new(
    const char *root,
    workspace_documents *documents,
    char *error,
    size_t size
)
{
    assert(((!root && !documents) || (root && documents)) && (!size || error));
    workspace_files_web_init();
    for (int i = 0; i < 8; i++) {
        if (!instances[i]) {
            assert(next_instance > 0 && next_instance < INT_MAX);
            browser_messages *service = xcalloc(1, sizeof *service);
            service->root = xstrdup(root ? root : "");
            service->documents = documents;
            service->instance = next_instance++;
            service->next_request = 1;
            instances[i] = service;
            if (size) {
                error[0] = 0;
            }
            return service;
        }
    }
    snprintf(error, size, "Browser message service limit exceeded");
    return NULL;
}
int browser_messages_instance(const browser_messages *service)
{
    return service->instance;
}
void browser_messages_free(browser_messages *service)
{
    if (!service) {
        return;
    }
#ifdef __EMSCRIPTEN__
    web_destroy(service->instance);
#endif
    for (int i = 0; i < 8; i++) {
        if (instances[i] == service) {
            instances[i] = NULL;
        }
    }
    for (int i = 0; i < service->count; i++) {
        clear(&service->queue[i]);
    }
    clear(&service->current);
    free(service->root);
    free(service);
}
bool browser_messages_available(const browser_messages *service)
{
    assert(service);
    (void)service;
#ifdef __EMSCRIPTEN__
    return web_available();
#else
    return false;
#endif
}
EXPORT bool browser_messages_deliver(
    int instance,
    int request,
    const char *channel,
    const char *kind,
    const char *text,
    int source
)
{
    browser_messages *service = NULL;
    for (int i = 0; i < 8; i++) {
        if (instances[i] && instances[i]->instance == instance) {
            service = instances[i];
            break;
        }
    }
    if (!service || request < 0 || source < 0 || !token(channel) ||
        !token(kind) || !text ||
        strnlen(text, BROWSER_MESSAGES_TEXT_MAX + 1) > BROWSER_MESSAGES_TEXT_MAX) {
        return false;
    }
    if (service->count == BROWSER_MESSAGES_QUEUE_MAX) {
        service->overflow = true;
        return false;
    }
    service->queue[service->count++] = (queued_message){ request,
                                                         source,
                                                         xstrdup(channel),
                                                         xstrdup(kind),
                                                         xstrdup(text) };
    return true;
}

bool browser_messages_request(
    browser_messages *service,
    const char *channel,
    const char *operation,
    const char *value,
    const char *json,
    const char *headers,
    int source,
    int *request,
    char *error,
    size_t size
)
{
    *request = 0;
    if (size) {
        error[0] = 0;
    }
    if (!token(channel) || !token(operation) || !value || !json || !headers ||
        source < 0 ||
        strnlen(value, BROWSER_MESSAGES_TEXT_MAX + 1) > BROWSER_MESSAGES_TEXT_MAX ||
        strnlen(json, BROWSER_MESSAGES_TEXT_MAX + 1) > BROWSER_MESSAGES_TEXT_MAX ||
        strnlen(headers, BROWSER_MESSAGES_TEXT_MAX + 1) > BROWSER_MESSAGES_TEXT_MAX) {
        snprintf(error, size, "Invalid or oversized browser message request");
        return false;
    }
    if (service && !service->documents && !strcmp(operation, "archive-capture")) {
        snprintf(error, size, "Open a workspace before capturing its archive");
        return false;
    }
    if (!browser_messages_available(service)) {
        snprintf(error, size, "Browser message transport unavailable on this host");
        return false;
    }
    static const char *const operations[] = {
        "connect",         "send",
        "close",           "http-post",
        "archive-capture", "archive-install",
        "configuration",   "location",
        "session-read",    "session-write",
        "compose-json",    "parse-url",
        "verify-json",     "make-url"
    };
    bool known = false;
    for (size_t i = 0; i < sizeof operations / sizeof *operations; i++) {
        known |= !strcmp(operation, operations[i]);
    }
    if (!known) {
        snprintf(error, size, "Unknown browser message operation");
        return false;
    }
    unsigned char *document_state = NULL;
    size_t document_state_bytes = 0;
    if (!strcmp(operation, "archive-capture")) {
        workspace_document_status status = workspace_documents_snapshot(
            service->documents,
            &document_state,
            &document_state_bytes
        );
        if (status) {
            snprintf(error, size, "Document checkpoint refused: %s", workspace_document_status_text(status));
            return false;
        }
    }
    assert(service->next_request > 0 && service->next_request < INT_MAX);
    int handle = service->next_request++;
#ifdef __EMSCRIPTEN__
    bool accepted = browser_messages_web_request(
        service->instance,
        handle,
        channel,
        operation,
        value,
        json,
        headers,
        source,
        service->root,
        document_state,
        document_state_bytes
    );
#else
    bool accepted = false;
    (void)source;
#endif
    free(document_state);
    if (!accepted) {
        snprintf(error, size, "Browser message request refused");
        return false;
    }
    *request = handle;
    return true;
}
browser_message browser_messages_receive(browser_messages *service, const char *channel)
{
    clear(&service->current);
    if (!token(channel)) {
        return (browser_message){ .kind = "", .text = "" };
    }
    if (service->overflow) {
        service->overflow = false;
        return (browser_message){
            .available = true,
            .kind = "overflow",
            .text = "Browser message queue overflow; resynchronize"
        };
    }
    for (int i = 0; i < service->count; i++) {
        if (strcmp(service->queue[i].channel, channel)) {
            continue;
        }
        service->current = service->queue[i];
        memmove(
            &service->queue[i],
            &service->queue[i + 1],
            (size_t)(service->count - i - 1) * sizeof *service->queue
        );
        service->count--;
        return (browser_message){ true,
                                  service->current.request,
                                  service->current.source,
                                  service->current.kind,
                                  service->current.text };
    }
    return (browser_message){ .kind = "", .text = "" };
}
bool browser_messages_query(
    browser_messages *service,
    int source,
    const char *pointer,
    json_query_result *result,
    char *error,
    size_t size
)
{
    json_query_result_free(result);
#ifdef __EMSCRIPTEN__
    char *text = web_text(service->instance, source);
    bool found = text != NULL;
    bool ok = found && query_json(text, pointer, result, error, size);
    free(text);
    if (ok && strlen(result->string) > BROWSER_MESSAGES_TEXT_MAX) {
        json_query_result_free(result);
        snprintf(error, size, "Selected attachment string exceeds the metadata limit");
        return false;
    }
    if (ok && strlen(result->json) > BROWSER_MESSAGES_TEXT_MAX) {
        // Large containers remain opaque; shape/count can still be inspected.
        free(result->json);
        result->json = xstrdup("");
    }
    if (!found) {
        snprintf(error, size, "Unknown browser message attachment");
    }
    return ok;
#else
    (void)service;
    (void)source;
    (void)pointer;
    snprintf(error, size, "Browser message transport unavailable on this host");
    return false;
#endif
}
bool browser_messages_release(browser_messages *service, int source)
{
#ifdef __EMSCRIPTEN__
    return web_release(service->instance, source);
#else
    (void)service;
    (void)source;
    return false;
#endif
}
