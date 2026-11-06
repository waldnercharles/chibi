#ifndef CHIBI_RUNTIME_URL_H
#define CHIBI_RUNTIME_URL_H

#include "execution/host.h"

#include <stdbool.h>

/* A host opts in before accepting authored requests. Accepted means queued,
 * never that a browser opened or loaded the page. Only HTTP(S) is supported. */
typedef struct url_requests url_requests;
url_requests *url_requests_new();
void url_requests_free(url_requests *requests);
/* Disabling also cancels pending work. New services start disabled. */
void url_requests_enable(url_requests *requests, bool enabled);
/* Copies the URL. Every call replaces or cancels the preceding request. */
bool url_requests_open(url_requests *requests, const char *url);
void url_requests_cancel(url_requests *requests);
/* True for a new request or cancellation. Transfers an owned URL to the host;
 * NULL means cancel any request the host is still waiting to launch. */
bool url_requests_take(url_requests *requests, char **url);

#define URL_SERVICE "host.urls"
extern const node_host_call URL_OPEN_CALL, URL_CANCEL_CALL;
extern const node_access URL_ACCESS;

#endif
