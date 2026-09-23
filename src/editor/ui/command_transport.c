#include "editor/ui/command_transport.h"

#include "support/xalloc.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <yyjson.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// clang-format off
EM_JS(int, web_start, (const char *json), {
    if (!Module.chibiCommandStart) {
        return 0;
    }
    return Module.chibiCommandStart(UTF8ToString(json));
})
EM_JS(char *, web_poll, (int id), {
    const result = Module.chibiCommandPoll?.(id);
    return result === undefined ? 0 : stringToNewUTF8(result);
})
EM_JS(void, web_cancel, (int id), { Module.chibiCommandCancel?.(id); })
// clang-format on
#else
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#define COMMAND_RESPONSE_LIMIT (1024 * 1024)
#define COMMAND_ERROR_SIZE 256

char *editor_command_response_body(const char *response, char *error, int error_size)
{
    const char *body = strstr(response, "\r\n\r\n");
    int status = 0;
    if (!body || sscanf(response, "HTTP/%*s %d", &status) != 1) {
        snprintf(error, (size_t)error_size, "Model host returned an invalid HTTP response");
        return NULL;
    }
    if (status < 200 || status >= 300) {
        snprintf(error, (size_t)error_size, "Model host returned HTTP %d", status);
        return NULL;
    }
    size_t content_length = 0;
    bool has_length = false, chunked = false;
    const char *header = strstr(response, "\r\n");
    while (header && header < body) {
        header += 2;
        const char *end = strstr(header, "\r\n");
        if (!end || end > body) {
            break;
        }
        if (end - header >= 16 && !strncasecmp(header, "Content-Length:", 15)) {
            char *number_end = NULL;
            unsigned long value = strtoul(header + 15, &number_end, 10);
            if (number_end == header + 15 || number_end > end ||
                value > COMMAND_RESPONSE_LIMIT) {
                snprintf(error, (size_t)error_size, "Model host returned an invalid content length");
                return NULL;
            }
            content_length = (size_t)value;
            has_length = true;
        }
        if (end - header >= 18 && !strncasecmp(header, "Transfer-Encoding:", 18)) {
            const char *value = header + 18;
            while (value < end && isspace((unsigned char)*value)) {
                value++;
            }
            chunked = end - value >= 7 && !strncasecmp(value, "chunked", 7);
        }
        header = end;
    }
    body += 4;
    size_t available = strlen(body);
    if (!chunked) {
        if (has_length && available < content_length) {
            snprintf(error, (size_t)error_size, "Model host returned an incomplete response");
            return NULL;
        }
        size_t size = has_length ? content_length : available;
        char *result = xmalloc(size + 1);
        memcpy(result, body, size);
        result[size] = 0;
        return result;
    }
    char *result = xmalloc(available + 1);
    size_t size = 0;
    const char *cursor = body;
    const char *limit = body + available;
    while (cursor < limit) {
        char *next = NULL;
        unsigned long chunk_size = strtoul(cursor, &next, 16);
        const char *line_end = strstr(cursor, "\r\n");
        if (next == cursor || !line_end || next > line_end ||
            chunk_size > (size_t)(limit - line_end - 2)) {
            break;
        }
        cursor = line_end + 2;
        if (!chunk_size) {
            result[size] = 0;
            return result;
        }
        memcpy(result + size, cursor, (size_t)chunk_size);
        size += (size_t)chunk_size;
        cursor += chunk_size;
        if (limit - cursor < 2 || cursor[0] != '\r' || cursor[1] != '\n') {
            break;
        }
        cursor += 2;
    }
    free(result);
    snprintf(error, (size_t)error_size, "Model host returned invalid chunked data");
    return NULL;
}

struct editor_command_request
{
    Uint64 started;
    char *immediate;
    bool local;
#ifdef __EMSCRIPTEN__
    int id;
#else
    char *url;
    char *body;
    char *output;
    SDL_Mutex *mutex;
    SDL_AtomicInt references;
    int socket;
    bool cancelled, complete, done;
#endif
};

#ifndef __EMSCRIPTEN__
static char *error_response(const char *message)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", false);
    yyjson_mut_obj_add_str(doc, root, "status", "failed");
    yyjson_mut_obj_add_str(doc, root, "error", message ? message : "Model host failed");
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

static void request_socket(editor_command_request *request, int socket)
{
    SDL_LockMutex(request->mutex);
    request->socket = socket;
    bool cancelled = request->cancelled;
    SDL_UnlockMutex(request->mutex);
    if (cancelled && socket >= 0) {
        shutdown(socket, SHUT_RDWR);
    }
}

static bool request_cancelled(editor_command_request *request)
{
    SDL_LockMutex(request->mutex);
    bool cancelled = request->cancelled;
    SDL_UnlockMutex(request->mutex);
    return cancelled;
}

static char *url_request_parts(
    const char *url,
    char *host,
    size_t host_size,
    char *port,
    size_t port_size,
    char *path,
    size_t path_size
)
{
    if (strncmp(url, "http://", 7)) {
        return xstrdup("Model host must use http://");
    }
    const char *authority = url + 7;
    const char *slash = strchr(authority, '/');
    size_t authority_size = slash ? (size_t)(slash - authority) : strlen(authority);
    if (!authority_size || authority_size >= host_size) {
        return xstrdup("Model host has an invalid address");
    }
    const char *colon = memchr(authority, ':', authority_size);
    if (colon) {
        size_t name_size = (size_t)(colon - authority);
        size_t port_length = authority_size - name_size - 1;
        if (!name_size || !port_length || name_size >= host_size ||
            port_length >= port_size) {
            return xstrdup("Model host has an invalid port");
        }
        memcpy(host, authority, name_size);
        host[name_size] = 0;
        memcpy(port, colon + 1, port_length);
        port[port_length] = 0;
    } else {
        memcpy(host, authority, authority_size);
        host[authority_size] = 0;
        snprintf(port, port_size, "80");
    }
    if (!slash || !slash[1]) {
        snprintf(path, path_size, "/");
    } else if (strlen(slash) >= path_size) {
        return xstrdup("Model host path is too long");
    } else {
        snprintf(path, path_size, "%s", slash);
    }
    return NULL;
}

static char *http_post(editor_command_request *request)
{
    char host[256], port[16], path[1024];
    char *error = url_request_parts(request->url, host, sizeof host, port, sizeof port, path, sizeof path);
    if (error) {
        char *result = error_response(error);
        free(error);
        return result;
    }
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *addresses = NULL;
    int lookup = getaddrinfo(host, port, &hints, &addresses);
    if (lookup) {
        return error_response(gai_strerror(lookup));
    }
    int socket_fd = -1;
    for (struct addrinfo *address = addresses; address; address = address->ai_next) {
        socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd < 0) {
            continue;
        }
        struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        request_socket(request, socket_fd);
        if (!connect(socket_fd, address->ai_addr, address->ai_addrlen)) {
            break;
        }
        close(socket_fd);
        request_socket(request, -1);
        socket_fd = -1;
        if (request_cancelled(request)) {
            break;
        }
    }
    freeaddrinfo(addresses);
    if (socket_fd < 0) {
        return error_response(
            request_cancelled(request) ? "Model request cancelled" : strerror(errno)
        );
    }
    char header[1536];
    int header_size = snprintf(
        header,
        sizeof header,
        "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n",
        path,
        host,
        strlen(request->body)
    );
    if (header_size < 0 || (size_t)header_size >= sizeof header) {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
        request_socket(request, -1);
        return error_response("Model request headers are too long");
    }
    size_t body_size = strlen(request->body);
    size_t sent = 0;
    while (sent < (size_t)header_size && !request_cancelled(request)) {
        ssize_t count = send(socket_fd, header + sent, (size_t)header_size - sent, 0);
        if (count <= 0) {
            shutdown(socket_fd, SHUT_RDWR);
            close(socket_fd);
            request_socket(request, -1);
            return error_response("Could not send the model request");
        }
        sent += (size_t)count;
    }
    sent = 0;
    while (sent < body_size && !request_cancelled(request)) {
        ssize_t count = send(socket_fd, request->body + sent, body_size - sent, 0);
        if (count <= 0) {
            shutdown(socket_fd, SHUT_RDWR);
            close(socket_fd);
            request_socket(request, -1);
            return error_response("Could not send the model request");
        }
        sent += (size_t)count;
    }
    if (request_cancelled(request)) {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
        request_socket(request, -1);
        return error_response("Model request cancelled");
    }
    size_t size = 0, capacity = 8192;
    char *response = xmalloc(capacity + 1);
    for (;;) {
        if (size == capacity) {
            if (capacity >= COMMAND_RESPONSE_LIMIT) {
                free(response);
                shutdown(socket_fd, SHUT_RDWR);
                close(socket_fd);
                request_socket(request, -1);
                return error_response("Model response is too large");
            }
            capacity *= 2;
            response = xrealloc(response, capacity + 1);
        }
        ssize_t count = recv(socket_fd, response + size, capacity - size, 0);
        if (count > 0) {
            size += (size_t)count;
            continue;
        }
        if (!count || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            break;
        }
        if (request_cancelled(request)) {
            break;
        }
    }
    response[size] = 0;
    shutdown(socket_fd, SHUT_RDWR);
    close(socket_fd);
    request_socket(request, -1);
    if (request_cancelled(request)) {
        free(response);
        return error_response("Model request cancelled");
    }
    char message[COMMAND_ERROR_SIZE];
    char *result = editor_command_response_body(response, message, sizeof message);
    free(response);
    return result ? result : error_response(message);
}

static void request_destroy(editor_command_request *request)
{
    SDL_DestroyMutex(request->mutex);
    free(request->url);
    free(request->body);
    free(request->output);
    free(request);
}

static void request_release(editor_command_request *request)
{
    if (SDL_AtomicDecRef(&request->references)) {
        request_destroy(request);
    }
}

static int model_thread(void *data)
{
    editor_command_request *request = data;
    char *output = http_post(request);
    SDL_LockMutex(request->mutex);
    if (request->cancelled) {
        free(output);
    } else {
        request->output = output;
    }
    request->done = true;
    SDL_UnlockMutex(request->mutex);
    request_release(request);
    return 0;
}

static void cancel(editor_command_request *request)
{
    SDL_LockMutex(request->mutex);
    request->cancelled = true;
    int socket = request->socket;
    SDL_UnlockMutex(request->mutex);
    if (socket >= 0) {
        shutdown(socket, SHUT_RDWR);
    }
}
#endif

editor_command_request *editor_command_request_start(const char *json)
{
    editor_command_request *request = xcalloc(1, sizeof *request);
    request->started = SDL_GetTicks();
    yyjson_doc *local_doc = yyjson_read(json, strlen(json), 0);
    const char *local = yyjson_get_str(
        yyjson_obj_get(yyjson_doc_get_root(local_doc), "local_response")
    );
    if (local) {
        request->immediate = xstrdup(local);
        request->local = true;
        yyjson_doc_free(local_doc);
        return request;
    }
    yyjson_doc_free(local_doc);
#ifdef __EMSCRIPTEN__
    request->id = web_start(json);
    if (!request->id) {
        free(request);
        return NULL;
    }
#else
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    const char *url = yyjson_get_str(yyjson_obj_get(root, "url"));
    const char *body = yyjson_get_str(yyjson_obj_get(root, "body"));
    if (!url || !body || !*url || !*body) {
        yyjson_doc_free(doc);
        free(request);
        return NULL;
    }
    request->url = xstrdup(url);
    request->body = xstrdup(body);
    request->socket = -1;
    request->mutex = SDL_CreateMutex();
    yyjson_doc_free(doc);
    if (!request->mutex) {
        free(request->url);
        free(request->body);
        free(request);
        return NULL;
    }
    /* DNS and connect can block outside socket timeouts, so cleanup cannot join. */
    SDL_SetAtomicInt(&request->references, 2);
    SDL_Thread *thread = SDL_CreateThread(model_thread, "chibi-command-model", request);
    if (!thread) {
        SDL_DestroyMutex(request->mutex);
        free(request->url);
        free(request->body);
        free(request);
        return NULL;
    }
    SDL_DetachThread(thread);
#endif
    return request;
}

char *editor_command_request_poll(editor_command_request *request)
{
    if (request->local) {
        char *result = request->immediate;
        request->immediate = NULL;
        return result;
    }
    if (SDL_GetTicks() - request->started > 30000) {
#ifdef __EMSCRIPTEN__
        return xstrdup("{\"ok\":false,\"error\":\"Command model timed out\"}");
#else
        cancel(request);
        return xstrdup("{\"ok\":false,\"error\":\"Command model timed out\"}");
#endif
    }
#ifdef __EMSCRIPTEN__
    return web_poll(request->id);
#else
    SDL_LockMutex(request->mutex);
    if (!request->done) {
        SDL_UnlockMutex(request->mutex);
        return NULL;
    }
    char *output = request->output;
    request->output = NULL;
    request->complete = true;
    SDL_UnlockMutex(request->mutex);
    return output;
#endif
}

void editor_command_request_free(editor_command_request *request)
{
    if (!request) {
        return;
    }
    if (request->local) {
        free(request->immediate);
        free(request);
        return;
    }
#ifdef __EMSCRIPTEN__
    web_cancel(request->id);
    free(request);
#else
    if (!request->complete) {
        cancel(request);
    }
    request_release(request);
#endif
}
