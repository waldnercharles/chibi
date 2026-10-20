#include "host/serve.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Connections waiting for a request at once, how long each may take from
 * accept to its last byte sent, and the largest request accepted. */
#define SERVE_CONNECTIONS 32
#define SERVE_SECONDS 10.0
#define SERVE_REQUEST_BYTES 8192
#define SERVE_PATH_BYTES 4096
/* How often a wait looks at the stop flag, in milliseconds. */
#define SERVE_TICK 200

typedef struct
{
    int socket; /* -1 for a free slot */
    double deadline;
    size_t used;
    char request[SERVE_REQUEST_BYTES];
} connection;

static double now()
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (double)value.tv_sec + value.tv_nsec / 1e9;
}

int chibi_serve_listen(int port, int *bound)
{
    int server = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_port = htons((uint16_t)port),
                                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    socklen_t size = sizeof address;
    int reuse = 1;
    bool listening = server >= 0 && !fcntl(server, F_SETFD, FD_CLOEXEC) &&
                     !setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse) &&
                     !bind(server, (struct sockaddr *)&address, sizeof address) &&
                     !listen(server, SERVE_CONNECTIONS) &&
                     !getsockname(server, (struct sockaddr *)&address, &size);
    if (!listening) {
        int error = errno;
        if (server >= 0) {
            close(server);
        }
        errno = error;
        return -1;
    }
    *bound = ntohs(address.sin_port);
    return server;
}

/* Sends all of bytes before the connection's deadline or a stop. */
static bool send_all(const connection *c, const void *bytes, size_t size, const volatile sig_atomic_t *stop)
{
    const char *data = bytes;
    while (size && !*stop && now() < c->deadline) {
        struct pollfd ready = { .fd = c->socket, .events = POLLOUT };
        if (poll(&ready, 1, SERVE_TICK) <= 0) {
            continue;
        }
        ssize_t sent = send(c->socket, data, size, 0);
        if (sent < 0 && (errno == EAGAIN || errno == EINTR)) {
            continue;
        }
        if (sent <= 0) {
            return false;
        }
        data += sent;
        size -= (size_t)sent;
    }
    return size == 0;
}

static void status(const connection *c, const char *line, const volatile sig_atomic_t *stop)
{
    char text[256];
    int length = snprintf(text, sizeof text, "HTTP/1.1 %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", line);
    send_all(c, text, (size_t)length, stop);
}

/* Browsers need wasm's own type to compile it as it streams in. */
static const char *content_type(const char *path)
{
    static const struct
    {
        const char *extension, *type;
    } TYPES[] = {
        { ".html", "text/html; charset=utf-8" },
        { ".css", "text/css" },
        { ".js", "text/javascript" },
        { ".wasm", "application/wasm" },
        { ".json", "application/json" },
        { ".png", "image/png" },
        { ".ogg", "audio/ogg" },
        { ".wav", "audio/wav" },
    };
    const char *extension = strrchr(path, '.');
    for (size_t i = 0; extension && i < sizeof TYPES / sizeof *TYPES; i++) {
        if (!strcmp(extension, TYPES[i].extension)) {
            return TYPES[i].type;
        }
    }
    return "application/octet-stream";
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* The request target as a path below the root, percent-decoded, with "/"
 * as index.html; false for a malformed target or one that leaves the root. */
static bool target_path(const char *target, char *path, size_t size)
{
    size_t length = 0;
    if (target[0] != '/') {
        return false;
    }
    for (const char *c = target + 1; *c && *c != '?' && *c != '#'; c++) {
        int value = (unsigned char)*c;
        if (*c == '%') {
            int high = hex(c[1]), low = high < 0 ? -1 : hex(c[2]);
            if (low < 0) {
                return false;
            }
            value = high * 16 + low;
            c += 2;
        }
        if (!value || value == '\\' || length + 1 >= size) {
            return false;
        }
        path[length++] = (char)value;
    }
    path[length] = '\0';
    if (!length) {
        snprintf(path, size, "index.html");
        return true;
    }
    for (const char *segment = path; segment; segment = strchr(segment, '/')) {
        segment += *segment == '/';
        if (!strncmp(segment, "..", 2) && (segment[2] == '/' || !segment[2])) {
            return false;
        }
    }
    return true;
}

/* Whether the request names this computer as its host, so a page whose name
 * was rebound to 127.0.0.1 cannot read the site. */
static bool local_host(const char *request)
{
    const char *line = strstr(request, "\r\n");
    while (line && strncasecmp(line + 2, "Host:", 5)) {
        line = strstr(line + 2, "\r\n");
    }
    if (!line) {
        return false;
    }
    const char *host = line + strlen("\r\nHost:");
    host += strspn(host, " \t");
    static const char *const NAMES[] = { "127.0.0.1", "localhost" };
    for (size_t i = 0; i < sizeof NAMES / sizeof *NAMES; i++) {
        size_t n = strlen(NAMES[i]);
        if (!strncmp(host, NAMES[i], n)) {
            const char *rest = host + n;
            rest += *rest == ':' ? 1 + strspn(rest + 1, "0123456789") : 0;
            return rest[0] == '\r';
        }
    }
    return false;
}

static void serve_file(
    const connection *c,
    const char *root,
    const char *name,
    bool body,
    const volatile sig_atomic_t *stop
)
{
    char path[SERVE_PATH_BYTES * 2];
    snprintf(path, sizeof path, "%s/%s", root, name);
    int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat info;
    if (file < 0 || fstat(file, &info) || !S_ISREG(info.st_mode)) {
        if (file >= 0) {
            close(file);
        }
        status(c, "404 Not Found", stop);
        return;
    }
    char header[512];
    int length = snprintf(
        header,
        sizeof header,
        "HTTP/1.1 200 OK\r\nContent-Length: %lld\r\nContent-Type: %s\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        (long long)info.st_size,
        content_type(name)
    );
    bool sent = send_all(c, header, (size_t)length, stop);
    char bytes[65536];
    for (ssize_t count;
         sent && body && (count = read(file, bytes, sizeof bytes)) > 0;) {
        sent = send_all(c, bytes, (size_t)count, stop);
    }
    close(file);
}

/* Answers a complete request: a GET or HEAD for this computer of a file below
 * the root, or the reason it is refused. */
static void answer(const connection *c, const char *root, const volatile sig_atomic_t *stop)
{
    char method[8], target[SERVE_PATH_BYTES], path[SERVE_PATH_BYTES];
    if (sscanf(c->request, "%7s %4095s HTTP/1.", method, target) != 2 ||
        !local_host(c->request)) {
        status(c, "400 Bad Request", stop);
        return;
    }
    bool get = !strcmp(method, "GET");
    if (!get && strcmp(method, "HEAD")) {
        status(c, "405 Method Not Allowed", stop);
        return;
    }
    if (!target_path(target, path, sizeof path)) {
        status(c, "400 Bad Request", stop);
        return;
    }
    serve_file(c, root, path, get, stop);
}

/* Reads what has arrived; true once the connection is done with, answered
 * or abandoned. */
static bool receive(connection *c, const char *root, const volatile sig_atomic_t *stop)
{
    ssize_t count = recv(c->socket, c->request + c->used, sizeof c->request - 1 - c->used, 0);
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
        return false;
    }
    if (count <= 0) {
        return true; /* Browsers open speculative connections. */
    }
    c->used += (size_t)count;
    c->request[c->used] = '\0';
    if (strstr(c->request, "\r\n\r\n")) {
        answer(c, root, stop);
        return true;
    }
    if (c->used == sizeof c->request - 1) {
        status(c, "431 Request Header Fields Too Large", stop);
        return true;
    }
    return false;
}

static void accept_connection(int server, connection *connections)
{
    int client = accept(server, NULL, NULL);
    if (client < 0) {
        return;
    }
    for (int i = 0; i < SERVE_CONNECTIONS; i++) {
        if (connections[i].socket < 0) {
            fcntl(client, F_SETFL, fcntl(client, F_GETFL) | O_NONBLOCK);
            fcntl(client, F_SETFD, FD_CLOEXEC);
            connections[i] = (connection){ client, now() + SERVE_SECONDS, 0, { 0 } };
            return;
        }
    }
    close(client); /* Every slot is waiting; the browser retries. */
}

void chibi_serve(int server, const char *root, const volatile sig_atomic_t *stop)
{
    /* Too large for the stack; one server runs per process. */
    static connection connections[SERVE_CONNECTIONS];
    for (int i = 0; i < SERVE_CONNECTIONS; i++) {
        connections[i].socket = -1;
    }
    signal(SIGPIPE, SIG_IGN);
    while (!*stop) {
        struct pollfd ready[SERVE_CONNECTIONS + 1] = { { .fd = server,
                                                         .events = POLLIN } };
        for (int i = 0; i < SERVE_CONNECTIONS; i++) {
            ready[i + 1] = (struct pollfd){ .fd = connections[i].socket,
                                            .events = POLLIN };
        }
        if (poll(ready, SERVE_CONNECTIONS + 1, SERVE_TICK) < 0) {
            continue;
        }
        for (int i = 0; i < SERVE_CONNECTIONS; i++) {
            connection *c = &connections[i];
            bool done = c->socket >= 0 &&
                        (now() >= c->deadline ||
                         (ready[i + 1].revents && receive(c, root, stop)));
            if (done) {
                close(c->socket);
                c->socket = -1;
            }
        }
        if (ready[0].revents & POLLIN) {
            accept_connection(server, connections);
        }
    }
    for (int i = 0; i < SERVE_CONNECTIONS; i++) {
        if (connections[i].socket >= 0) {
            close(connections[i].socket);
        }
    }
    close(server);
}
