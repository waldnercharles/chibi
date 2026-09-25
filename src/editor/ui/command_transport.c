/* Model requests for editor commands. Native builds POST to a loopback HTTP
 * model host on a detached thread; the browser build hands the request to the
 * page, which forwards it through the development server. */
#include "editor/ui/command_transport.h"

#include "support/xalloc.h"

#include <SDL3/SDL.h>
#include <assert.h>
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
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#define COMMAND_RESPONSE_LIMIT (1024 * 1024)
#define COMMAND_ERROR_SIZE 256
#define COMMAND_TIMEOUT_MS 30000u

/* The value of a header before the body, or NULL. */
static const char *header_value(const char *response, const char *body, const char *name)
{
    size_t length = strlen(name);
    for (const char *line = strstr(response, "\r\n"); line && line < body;
         line = strstr(line + 2, "\r\n")) {
        const char *start = line + 2;
        if (start < body && !strncasecmp(start, name, length) && start[length] == ':') {
            start += length + 1;
            while (*start == ' ' || *start == '\t') {
                start++;
            }
            return start;
        }
    }
    return NULL;
}

static char *dechunk(const char *body, char *error, int error_size)
{
    size_t available = strlen(body);
    char *result = xmalloc(available + 1);
    size_t size = 0;
    const char *cursor = body, *limit = body + available;
    while (cursor < limit) {
        char *next = NULL;
        unsigned long chunk = strtoul(cursor, &next, 16);
        const char *line_end = strstr(cursor, "\r\n");
        if (next == cursor || !line_end || next > line_end ||
            chunk > (size_t)(limit - line_end - 2)) {
            break;
        }
        cursor = line_end + 2;
        if (!chunk) {
            result[size] = 0;
            return result;
        }
        memcpy(result + size, cursor, chunk);
        size += chunk;
        cursor += chunk;
        if (limit - cursor < 2 || cursor[0] != '\r' || cursor[1] != '\n') {
            break;
        }
        cursor += 2;
    }
    free(result);
    snprintf(error, (size_t)error_size, "Model host returned invalid chunked data");
    return NULL;
}

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
    const char *encoding = header_value(response, body, "Transfer-Encoding");
    const char *length_value = header_value(response, body, "Content-Length");
    body += 4;
    if (encoding && !strncasecmp(encoding, "chunked", 7)) {
        return dechunk(body, error, error_size);
    }
    size_t available = strlen(body), size = available;
    if (length_value) {
        char *end = NULL;
        unsigned long length = strtoul(length_value, &end, 10);
        if (end == length_value || length > COMMAND_RESPONSE_LIMIT) {
            snprintf(error, (size_t)error_size, "Model host returned an invalid content length");
            return NULL;
        }
        if (available < length) {
            snprintf(error, (size_t)error_size, "Model host returned an incomplete response");
            return NULL;
        }
        size = length;
    }
    char *result = xmalloc(size + 1);
    memcpy(result, body, size);
    result[size] = 0;
    return result;
}

struct editor_command_request
{
    Uint64 started;
    char *immediate; /* a decision made without the model */
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
    yyjson_mut_obj_add_strcpy(doc, root, "error", message);
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

static void close_socket(editor_command_request *request, int socket)
{
    shutdown(socket, SHUT_RDWR);
    close(socket);
    request_socket(request, -1);
}

/* Splits http://host[:port][/path]; returns an error message or NULL. */
static const char *url_parts(const char *url, char host[256], char port[16], char path[1024])
{
    if (strncmp(url, "http://", 7)) {
        return "Model host must use http://";
    }
    const char *authority = url + 7;
    const char *slash = strchr(authority, '/');
    size_t size = slash ? (size_t)(slash - authority) : strlen(authority);
    const char *colon = memchr(authority, ':', size);
    size_t name = colon ? (size_t)(colon - authority) : size;
    size_t port_length = colon ? size - name - 1 : 0;
    if (!name || name >= 256 || (colon && (!port_length || port_length >= 16))) {
        return "Model host has an invalid address";
    }
    if (slash && strlen(slash) >= 1024) {
        return "Model host path is too long";
    }
    snprintf(host, 256, "%.*s", (int)name, authority);
    snprintf(port, 16, "%.*s", colon ? (int)port_length : 2, colon ? colon + 1 : "80");
    snprintf(path, 1024, "%s", slash ? slash : "/");
    return NULL;
}

static int connect_host(editor_command_request *request, const char *host, const char *port)
{
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *addresses = NULL;
    if (getaddrinfo(host, port, &hints, &addresses)) {
        return -1;
    }
    int socket_fd = -1;
    for (struct addrinfo *address = addresses; address && !request_cancelled(request);
         address = address->ai_next) {
        socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd < 0) {
            continue;
        }
        struct timeval timeout = { .tv_sec = 1 };
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        request_socket(request, socket_fd);
        if (!connect(socket_fd, address->ai_addr, address->ai_addrlen)) {
            break;
        }
        close(socket_fd);
        request_socket(request, -1);
        socket_fd = -1;
    }
    freeaddrinfo(addresses);
    return socket_fd;
}

static bool send_all(editor_command_request *request, int socket, const char *data, size_t size)
{
    for (size_t sent = 0; sent < size;) {
        ssize_t count = send(socket, data + sent, size - sent, 0);
        if (count <= 0 || request_cancelled(request)) {
            return false;
        }
        sent += (size_t)count;
    }
    return true;
}

/* Reads until the host closes the connection; NULL past the size limit. */
static char *receive_all(editor_command_request *request, int socket)
{
    size_t size = 0, capacity = 8192;
    char *response = xmalloc(capacity + 1);
    for (;;) {
        if (size == capacity) {
            if (capacity >= COMMAND_RESPONSE_LIMIT) {
                free(response);
                return NULL;
            }
            capacity *= 2;
            response = xrealloc(response, capacity + 1);
        }
        ssize_t count = recv(socket, response + size, capacity - size, 0);
        if (count > 0) {
            size += (size_t)count;
            continue;
        }
        bool waiting = count < 0 &&
                       (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
        if (!waiting || request_cancelled(request)) {
            break;
        }
    }
    response[size] = 0;
    return response;
}

static char *http_post(editor_command_request *request)
{
    char host[256], port[16], path[1024];
    const char *invalid = url_parts(request->url, host, port, path);
    if (invalid) {
        return error_response(invalid);
    }
    int socket = connect_host(request, host, port);
    if (socket < 0) {
        return error_response(
            request_cancelled(request) ? "Model request cancelled" : "Could not reach the model host"
        );
    }
    char header[1536];
    size_t body_size = strlen(request->body);
    int header_size = snprintf(
        header,
        sizeof header,
        "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n",
        path,
        host,
        body_size
    );
    if (header_size < 0 || (size_t)header_size >= sizeof header ||
        !send_all(request, socket, header, (size_t)header_size) ||
        !send_all(request, socket, request->body, body_size)) {
        close_socket(request, socket);
        return error_response("Could not send the model request");
    }
    char *response = receive_all(request, socket);
    close_socket(request, socket);
    if (!response) {
        return error_response("Model response is too large");
    }
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

static bool start_thread(editor_command_request *request, yyjson_val *root)
{
    const char *url = yyjson_get_str(yyjson_obj_get(root, "url"));
    const char *body = yyjson_get_str(yyjson_obj_get(root, "body"));
    if (!url || !body || !*url || !*body) {
        return false;
    }
    request->url = xstrdup(url);
    request->body = xstrdup(body);
    request->socket = -1;
    request->mutex = SDL_CreateMutex();
    assert(request->mutex);
    /* DNS and connect can block outside socket timeouts, so cleanup cannot join. */
    SDL_SetAtomicInt(&request->references, 2);
    SDL_Thread *thread = SDL_CreateThread(model_thread, "chibi-command-model", request);
    if (!thread) {
        SDL_DestroyMutex(request->mutex);
        free(request->url);
        free(request->body);
        return false;
    }
    SDL_DetachThread(thread);
    return true;
}
#endif

editor_command_request *editor_command_request_start(const char *json)
{
    editor_command_request *request = xcalloc(1, sizeof *request);
    request->started = SDL_GetTicks();
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *local = yyjson_get_str(yyjson_obj_get(root, "local_response"));
    bool started = true;
    if (local) {
        request->immediate = xstrdup(local);
        request->local = true;
    } else {
#ifdef __EMSCRIPTEN__
        request->id = web_start(json);
        started = request->id != 0;
#else
        started = start_thread(request, root);
#endif
    }
    yyjson_doc_free(doc);
    if (!started) {
        free(request);
        return NULL;
    }
    return request;
}

char *editor_command_request_poll(editor_command_request *request)
{
    if (request->local) {
        char *result = request->immediate;
        request->immediate = NULL;
        return result;
    }
    if (SDL_GetTicks() - request->started > COMMAND_TIMEOUT_MS) {
#ifndef __EMSCRIPTEN__
        cancel(request);
#endif
        return xstrdup("{\"ok\":false,\"error\":\"Command model timed out\"}");
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
