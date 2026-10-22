#include "runtime/url.h"

#include "execution/function.h"
#include "support/xalloc.h"
#include "types/text.h"

#include <stdlib.h>
#include <string.h>

#define URL_MAX_BYTES 4096

struct url_requests
{
    char *pending;
    bool enabled, changed;
};

url_requests *url_requests_new()
{
    return xcalloc(1, sizeof(url_requests));
}

void url_requests_free(url_requests *requests)
{
    free(requests->pending);
    free(requests);
}

void url_requests_cancel(url_requests *requests)
{
    free(requests->pending);
    requests->pending = NULL;
    requests->changed = true;
}

void url_requests_enable(url_requests *requests, bool enabled)
{
    requests->enabled = enabled;
    if (!enabled) {
        url_requests_cancel(requests);
    }
}

/* Restrict the protocol and authority without interpreting URLs as shell
 * commands. The browser resolves host names and validates the remaining URL. */
static bool valid(const char *url)
{
    if (!url) {
        return false;
    }
    const char *host;
    if (!strncmp(url, "https://", 8)) {
        host = url + 8;
    } else if (!strncmp(url, "http://", 7)) {
        host = url + 7;
    } else {
        return false;
    }
    size_t length = 0;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
        if (*p <= ' ' || *p == 127 || *p == '\\' || ++length > URL_MAX_BYTES) {
            return false;
        }
    }
    size_t authority = strcspn(host, "/?#");
    if (!authority || host[0] == ':' || host[authority - 1] == ':' ||
        memchr(host, '@', authority)) {
        return false;
    }
    return true;
}

bool url_requests_open(url_requests *requests, const char *url)
{
    url_requests_cancel(requests);
    if (!requests->enabled || !valid(url)) {
        return false;
    }
    requests->pending = xstrdup(url);
    return true;
}

bool url_requests_take(url_requests *requests, char **url)
{
    *url = NULL;
    if (!requests->changed) {
        return false;
    }
    *url = requests->pending;
    requests->pending = NULL;
    requests->changed = false;
    return true;
}

static const char *open_url(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    const char *text;
    const char *error = pin_text_cstring(ctx->value_storage, in[0], &text, NULL);
    if (error) {
        return error;
    }

    out[0].b = service && url_requests_open(service, text);
    return NULL;
}

static const char *cancel(void *service, const pin_value *, pin_value *)
{
    if (service) {
        url_requests_cancel(service);
    }
    return NULL;
}

const node_host_call URL_OPEN_CALL = { .service = URL_SERVICE,
                                       .context = open_url,
                                       .optional_service = true };
const node_host_call URL_CANCEL_CALL = { .service = URL_SERVICE,
                                         .invoke = cancel,
                                         .optional_service = true };
const node_access URL_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
