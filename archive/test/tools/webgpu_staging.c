#include "host/process.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
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

static bool request(int client, double deadline, const char *root, char result[256])
{
    char input[8192];
    int used = 0;
    char *body = NULL;
    while (used < (int)sizeof input - 1 && now() < deadline) {
        ssize_t count = recv(client, input + used, sizeof input - 1 - (size_t)used, 0);
        if (count <= 0) {
            return false;
        }
        used += (int)count;
        input[used] = '\0';
        body = strstr(input, "\r\n\r\n");
        if (body) {
            break;
        }
    }
    if (!body) {
        return false;
    }
    body += 4;
    char *line_end = strstr(input, "\r\n");
    fprintf(stderr, "WebGPU HTTP request: %.*s\n", line_end ? (int)(line_end - input) : 0, input);
    bool diagnostic = !strncmp(input, "POST /diagnostic HTTP/1.1\r\n", 27);
    if (diagnostic || !strncmp(input, "POST /result HTTP/1.1\r\n", 23)) {
        int length = -1;
        for (char *line = strstr(input, "\r\n"); line && line < body - 2;
             line = strstr(line + 2, "\r\n")) {
            if (!strncasecmp(line + 2, "Content-Length:", 15)) {
                char extra;
                if (sscanf(line + 17, " %d%c", &length, &extra) != 2 || extra != '\r') {
                    return false;
                }
                break;
            }
        }
        if (length <= 0 || length >= 256 || used - (body - input) > length) {
            return false;
        }
        int have = (int)(input + used - body);
        memcpy(result, body, (size_t)have);
        while (have < length && now() < deadline) {
            ssize_t count = recv(client, result + have, (size_t)(length - have), 0);
            if (count <= 0) {
                return false;
            }
            have += (int)count;
        }
        result[have] = '\0';
        static const char
            response[] = "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
        bool sent = have == length &&
                    send_all(client, response, sizeof response - 1, deadline);
        fprintf(
            stderr,
            "WebGPU HTTP %s: sent=%d body=%.*s\n",
            diagnostic ? "diagnostic" : "result",
            sent,
            have,
            result
        );
        return sent && !diagnostic;
    }
    const char *names[] = { "test_tools.html", "test_tools.js", "test_tools.wasm" };
    const char *types[] = { "text/html", "text/javascript", "application/wasm" };
    for (int i = 0; i < 3; i++) {
        char prefix[96];
        snprintf(prefix, sizeof prefix, "GET /%s HTTP/1.1\r\n", names[i]);
        if (strncmp(input, prefix, strlen(prefix))) {
            continue;
        }
        char path[2048];
        snprintf(path, sizeof path, "%s/%s", root, names[i]);
        int file = open(path, O_RDONLY | O_NOFOLLOW);
        struct stat info;
        if (file < 0) {
            return false;
        }
        if (fstat(file, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
            info.st_size > 64 * 1024 * 1024) {
            close(file);
            return false;
        }
        char header[256];
        int length = snprintf(
            header,
            sizeof header,
            "HTTP/1.1 200 OK\r\nContent-Length: %lld\r\nContent-Type: "
            "%s\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
            (long long)info.st_size,
            types[i]
        );
        bool sent = send_all(client, header, (size_t)length, deadline);
        char bytes[65536];
        ssize_t count;
        while (sent && (count = read(file, bytes, sizeof bytes)) > 0) {
            sent = send_all(client, bytes, (size_t)count, deadline);
        }
        fprintf(
            stderr,
            "WebGPU HTTP file: %s bytes=%lld sent=%d\n",
            names[i],
            (long long)info.st_size,
            sent
        );
        close(file);
        return false; /* Only result POST can complete the test. */
    }
    static const char missing[] = "HTTP/1.1 404 Not Found\r\nContent-Length: "
                                  "0\r\nConnection: close\r\n\r\n";
    send_all(client, missing, sizeof missing - 1, deadline);
    return false;
}

TEST_CASE(browser_reuses_webgpu_staging)
{
    char expected[PATH_MAX], root[PATH_MAX], candidate[PATH_MAX];
    snprintf(candidate, sizeof candidate, "%s/build-webgpu-test", tool_root());
    const char *selected = getenv("CHIBI_WEBGPU_TEST_ROOT");
    if (!selected) {
        selected = candidate;
    }
    REQUIRE(selected[0] == '/' && realpath(selected, root));
    bool fixed = realpath(candidate, expected) && !strcmp(root, expected);
    bool baseline = realpath("/tmp/chibi-publish-validation/asyncify-baseline/build-webgpu-test", expected) &&
                    !strcmp(root, expected);
    REQUIRE(fixed || baseline);
    char directory[] = "/tmp/chibi-webgpu-staging-XXXXXX";
    REQUIRE(mkdtemp(directory));
    double deadline = now() + 55;
    const char *browser = getenv("CHIBI_WEBGPU_TEST_BROWSER");
    if (!browser) {
        browser = "/Applications/Google Chrome.app/Contents/MacOS/Google "
                  "Chrome";
    }
    bool available = browser[0] == '/' && !access(browser, X_OK);
    if (!available) {
        tool_remove_tree(directory);
        REQUIRE(available);
    }
    int server = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    socklen_t size = sizeof address;
    bool listening = server >= 0 &&
                     !bind(server, (struct sockaddr *)&address, sizeof address) &&
                     !listen(server, 8) &&
                     !getsockname(server, (struct sockaddr *)&address, &size);
    if (!listening) {
        if (server >= 0) {
            close(server);
        }
        tool_remove_tree(directory);
        REQUIRE(listening);
    }
    char profile[2048], url[128], log[2048];
    snprintf(profile, sizeof profile, "--user-data-dir=%s/profile", directory);
    snprintf(log, sizeof log, "%s/chrome.log", directory);
    snprintf(
        url,
        sizeof url,
        "http://127.0.0.1:%d/test_tools.html",
        ntohs(address.sin_port)
    );
    const char *args[] = { browser,
                           "--headless=new",
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
    bool started = chibi_process_start(&process, args, tool_root(), log);
    char result[256] = { 0 };
    bool complete = false;
    if (started) {
        tool_own_process(process.pid);
        void (*previous_pipe)(int) = signal(SIGPIPE, SIG_IGN);
        fprintf(stderr, "WebGPU browser: %s root=%s url=%s\n", browser, root, url);
        for (int requests = 0; requests < 64 && now() < deadline &&
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
            requests++;
            complete = request(client, deadline, root, result);
            close(client);
            if (complete) {
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
    FILE *output = fopen(log, "r");
    if (output) {
        char bytes[65537];
        assert(!fseek(output, 0, SEEK_END));
        long length = ftell(output);
        assert(length >= 0);
        assert(!fseek(output, length > 65536 ? length - 65536 : 0, SEEK_SET));
        size_t count = fread(bytes, 1, sizeof bytes - 1, output);
        bytes[count] = '\0';
        fputs(bytes, stderr);
        fclose(output);
    }
    tool_remove_tree(directory);
    REQUIRE(started && complete);
    int status, frames, pending, free_chunks;
    unsigned long long peak;
    double total_ms, max_ms;
    char extra;
    REQUIRE(
        sscanf(
            result,
            "%d %d %llu %d %d %lf %lf %c",
            &status,
            &frames,
            &peak,
            &pending,
            &free_chunks,
            &total_ms,
            &max_ms,
            &extra
        ) == 7
    );
    printf(
        "\nWebGPU staging: status=%d frames=%d peak=%llu pending=%d free=%d "
        "callback_wall_total_ms=%.3f callback_wall_max_ms=%.3f\n",
        status,
        frames,
        peak,
        pending,
        free_chunks,
        total_ms,
        max_ms
    );
    REQUIRE(status == 0);
    REQUIRE(frames == 120);
    return true;
}

void test_tool_webgpu_staging()
{
    RUN_TEST_CASE(browser_reuses_webgpu_staging);
}
