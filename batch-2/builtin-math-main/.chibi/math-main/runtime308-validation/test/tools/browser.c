#include "test/tools/browser.h"

#include "host/process.h"
#include "test/tools/fixture.h"

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define BROWSER_SECONDS 55
#define BROWSER_REQUESTS 64
#define BROWSER_FILE_LIMIT (64 * 1024 * 1024)

/* The loopback HTTP/process pattern matches webgpu_staging.c. This fixture
 * only transports C test output and deliberately disables GPU use. */
static double now()
{
    struct timespec value;
    assert(!clock_gettime(CLOCK_MONOTONIC, &value));
    return value.tv_sec + value.tv_nsec / 1e9;
}

static bool send_all(int client, const void *bytes, size_t size, double deadline)
{
    const char *data = bytes;
    while (size && now() < deadline) {
        ssize_t sent = send(client, data, size, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        size -= (size_t)sent;
    }
    return size == 0;
}

static bool headers(int client, const char *type, size_t size, double deadline)
{
    char header[512];
    int length = snprintf(
        header,
        sizeof header,
        "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nContent-Type: %s\r\n"
        "Cross-Origin-Opener-Policy: same-origin\r\n"
        "Cross-Origin-Embedder-Policy: require-corp\r\n"
        "Connection: close\r\nCache-Control: no-store\r\n\r\n",
        size,
        type
    );
    assert(length > 0 && length < (int)sizeof header);
    return send_all(client, header, (size_t)length, deadline);
}

static bool serve_file(int client, const char *root, const char *name, double deadline)
{
    char path[PATH_MAX];
    int length = snprintf(path, sizeof path, "%s/%s", root, name);
    if (length < 0 || length >= (int)sizeof path) {
        return false;
    }
    int file = open(path, O_RDONLY | O_NOFOLLOW);
    if (file < 0) {
        return false;
    }
    struct stat info;
    bool valid = !fstat(file, &info) && S_ISREG(info.st_mode) &&
                 info.st_size > 0 && info.st_size <= BROWSER_FILE_LIMIT;
    const char *type = strstr(name, ".wasm") ? "application/wasm" : "text/javascript";
    bool sent = valid && headers(client, type, (size_t)info.st_size, deadline);
    char bytes[65536];
    ssize_t count = 0;
    while (sent && (count = read(file, bytes, sizeof bytes)) > 0) {
        sent = send_all(client, bytes, (size_t)count, deadline);
    }
    close(file);
    return sent && count == 0;
}

static int request(
    int client,
    double deadline,
    const char *root,
    const char *page,
    const char *const files[],
    char *result,
    size_t capacity
)
{
    char input[8192];
    size_t used = 0;
    char *body = NULL;
    while (used < sizeof input - 1 && now() < deadline) {
        ssize_t count = recv(client, input + used, sizeof input - 1 - used, 0);
        if (count <= 0) {
            return 0; /* Chrome can open speculative connections. */
        }
        used += (size_t)count;
        input[used] = '\0';
        body = strstr(input, "\r\n\r\n");
        if (body) {
            break;
        }
    }
    if (!body) {
        return -1;
    }
    body += 4;
    if (!strncmp(input, "POST /result HTTP/1.1\r\n", 23)) {
        size_t length = 0;
        for (char *line = strstr(input, "\r\n"); line && line < body - 2;
             line = strstr(line + 2, "\r\n")) {
            if (!strncasecmp(line + 2, "Content-Length:", 15)) {
                char extra;
                if (sscanf(line + 17, " %zu%c", &length, &extra) != 2 || extra != '\r') {
                    return -1;
                }
                break;
            }
        }
        size_t have = used - (size_t)(body - input);
        if (!length || length >= capacity || have > length) {
            return -1;
        }
        memcpy(result, body, have);
        while (have < length && now() < deadline) {
            ssize_t count = recv(client, result + have, length - have, 0);
            if (count <= 0) {
                return -1;
            }
            have += (size_t)count;
        }
        result[have] = '\0';
        return have == length && headers(client, "text/plain", 0, deadline) ? 1 : -1;
    }
    char path[256];
    if (sscanf(input, "GET %255s HTTP/1.1\r\n", path) != 1) {
        return -1;
    }
    char *query = strchr(path, '?');
    if (query) {
        *query = '\0';
    }
    if (!strcmp(path, "/")) {
        return headers(client, "text/html", strlen(page), deadline) &&
                       send_all(client, page, strlen(page), deadline)
                 ? 0
                 : -1;
    }
    for (int i = 0; files[i]; i++) {
        if (path[0] == '/' && !strcmp(path + 1, files[i])) {
            return serve_file(client, root, files[i], deadline) ? 0 : -1;
        }
    }
    static const char
        missing[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                    "Connection: close\r\n\r\n";
    send_all(client, missing, sizeof missing - 1, deadline);
    return 0;
}

static void print_log(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }
    assert(!fseek(file, 0, SEEK_END));
    long size = ftell(file);
    assert(size >= 0 && !fseek(file, size > 16384 ? size - 16384 : 0, SEEK_SET));
    char bytes[16385];
    size_t count = fread(bytes, 1, sizeof bytes - 1, file);
    bytes[count] = '\0';
    fputs(bytes, stderr);
    fclose(file);
}

bool tool_browser(const char *root, const char *page, const char *const files[], char *result, size_t capacity)
{
    const char *browser = getenv("CHIBI_TEST_BROWSER");
    if (!browser) {
#ifdef __APPLE__
        browser = "/Applications/Google Chrome.app/Contents/MacOS/Google "
                  "Chrome";
#else
        browser = "/usr/bin/chromium";
#endif
    }
    if (browser[0] != '/' || access(browser, X_OK)) {
        fprintf(stderr, "Browser is unavailable: %s\n", browser);
        return false;
    }
    char directory[] = "/tmp/chibi-browser-XXXXXX";
    assert(mkdtemp(directory));
    int server = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    socklen_t size = sizeof address;
    bool listening = server >= 0 && !fcntl(server, F_SETFD, FD_CLOEXEC) &&
                     !bind(server, (struct sockaddr *)&address, sizeof address) &&
                     !listen(server, 8) &&
                     !getsockname(server, (struct sockaddr *)&address, &size);
    if (!listening) {
        if (server >= 0) {
            close(server);
        }
        tool_remove_tree(directory);
        return false;
    }
    char profile[PATH_MAX], url[128], log[PATH_MAX];
    snprintf(profile, sizeof profile, "--user-data-dir=%s/profile", directory);
    snprintf(log, sizeof log, "%s/chrome.log", directory);
    snprintf(url, sizeof url, "http://127.0.0.1:%d/", ntohs(address.sin_port));
    const char *args[] = { browser,
                           "--headless=new",
                           "--disable-gpu",
                           "--disable-software-rasterizer",
                           "--no-first-run",
                           "--no-default-browser-check",
                           "--disable-background-networking",
                           "--disable-background-timer-throttling",
                           "--disable-renderer-backgrounding",
                           "--enable-logging=stderr",
                           profile,
                           url,
                           NULL };
    chibi_process process;
    double deadline = now() + BROWSER_SECONDS;
    bool started = chibi_process_start(&process, args, tool_root(), log);
    bool complete = false;
    if (started) {
        tool_own_process(process.pid);
        void (*previous_pipe)(int) = signal(SIGPIPE, SIG_IGN);
        for (int requests = 0; requests < BROWSER_REQUESTS && now() < deadline &&
                               !chibi_process_poll(&process);) {
            struct pollfd ready = { .fd = server, .events = POLLIN };
            if (poll(&ready, 1, 100) <= 0) {
                continue;
            }
            int client = accept(server, NULL, NULL);
            if (client < 0) {
                continue;
            }
            struct timeval timeout = { .tv_sec = 1 };
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
            int status = request(client, deadline, root, page, files, result, capacity);
            close(client);
            requests++;
            if (status) {
                complete = status > 0;
                break;
            }
        }
        kill(-process.pid, SIGKILL);
        while (!chibi_process_poll(&process)) {
            struct timespec pause = { .tv_nsec = 1000000 };
            nanosleep(&pause, NULL);
        }
        tool_release_process(process.pid);
        signal(SIGPIPE, previous_pipe);
    }
    close(server);
    if (!complete) {
        print_log(log);
    }
    tool_remove_tree(directory);
    return complete;
}
