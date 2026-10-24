#include "host/process.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <yyjson.h>

static chibi_process server;
static int port;
static char directory[] = "/tmp/chibi-collab-XXXXXX";
static char log_path[sizeof directory + sizeof "/server.log"];
static yyjson_doc *response;
static char startup_error[256];
static char request_error[256];
static size_t response_bytes;
static int response_status;
static int request_system_error;
static bool passed;

static int request_failed(const char *stage, int error)
{
    request_system_error = error;
    snprintf(
        request_error,
        sizeof request_error,
        "%s: %s (errno %d)",
        stage,
        error ? strerror(error) : "invalid response",
        error
    );
    return 0;
}

static uint64_t milliseconds()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void pause_poll()
{
    struct timespec interval = { .tv_nsec = 20000000 };
    nanosleep(&interval, NULL);
}

static int request(const char *method, const char *path, const char *token, const char *body)
{
    yyjson_doc_free(response);
    response = NULL;
    request_error[0] = '\0';
    response_bytes = 0;
    response_status = 0;
    request_system_error = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    struct timeval timeout = { .tv_sec = 1 };
    assert(!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout));
    assert(!setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout));
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    assert(!setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof no_sigpipe));
#endif
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_port = htons(port),
                                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    if (connect(fd, (struct sockaddr *)&address, sizeof address)) {
        int error = errno;
        close(fd);
        return request_failed("connect", error);
    }
    char outgoing[8192], incoming[16384];
    int length = snprintf(
        outgoing,
        sizeof outgoing,
        "%s %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n"
        "Content-Type: application/json\r\nContent-Length: %zu\r\n"
        "%s%s%s\r\n%s",
        method,
        path,
        strlen(body),
        token ? "Authorization: Bearer " : "",
        token ? token : "",
        token ? "\r\n" : "",
        body
    );
    assert(length > 0 && length < (int)sizeof outgoing);
    size_t sent = 0, received = 0;
    while (sent < (size_t)length) {
#ifdef MSG_NOSIGNAL
        ssize_t size = send(fd, outgoing + sent, length - sent, MSG_NOSIGNAL);
#else
        ssize_t size = send(fd, outgoing + sent, length - sent, 0);
#endif
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size <= 0) {
            int error = size < 0 ? errno : EIO;
            close(fd);
            return request_failed("send", error);
        }
        sent += (size_t)size;
    }
    while (received < sizeof incoming - 1) {
        ssize_t size = recv(fd, incoming + received, sizeof incoming - 1 - received, 0);
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size < 0) {
            int error = errno;
            close(fd);
            incoming[received] = '\0';
            sscanf(incoming, "HTTP/1.1 %d", &response_status);
            return request_failed("recv", error);
        }
        if (!size) {
            break;
        }
        received += (size_t)size;
        response_bytes = received;
    }
    close(fd);
    incoming[received] = '\0';
    if (received == sizeof incoming - 1) {
        return request_failed("response exceeds buffer", 0);
    }
    if (sscanf(incoming, "HTTP/1.1 %d", &response_status) != 1) {
        return request_failed("HTTP status", 0);
    }
    char *json = strstr(incoming, "\r\n\r\n");
    if (!json) {
        return request_failed("HTTP headers", 0);
    }
    yyjson_read_err error;
    response = yyjson_read_opts(json + 4, strlen(json + 4), 0, NULL, &error);
    if (!response) {
        snprintf(
            request_error,
            sizeof request_error,
            "JSON at byte %zu: %s",
            error.pos,
            error.msg
        );
        return 0;
    }
    return response_status;
}

static bool expect_status(const char *method, const char *path, const char *token, const char *body, int expected)
{
    int actual = request(method, path, token, body);
    if (actual == expected) {
        return true;
    }
    fprintf(
        stderr,
        "collab: %s %s expected HTTP %d, got %d; parsed HTTP %d, %zu bytes; "
        "%s\n",
        method,
        path,
        expected,
        actual,
        response_status,
        response_bytes,
        request_error[0] ? request_error : "unexpected HTTP status"
    );
    const char *error = yyjson_get_str(
        yyjson_obj_get(yyjson_doc_get_root(response), "error")
    );
    if (error) {
        fprintf(stderr, "collab: response error: %s\n", error);
    }
    return false;
}

static const char *field(const char *name)
{
    return yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(response), name));
}

static void setup()
{
    server = (chibi_process){ 0 };
    startup_error[0] = '\0';
    request_error[0] = '\0';
    passed = false;
    strcpy(directory, "/tmp/chibi-collab-XXXXXX");
    assert(mkdtemp(directory));
    snprintf(log_path, sizeof log_path, "%s/server.log", directory);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    assert(!bind(fd, (struct sockaddr *)&address, sizeof address));
    socklen_t size = sizeof address;
    assert(!getsockname(fd, (struct sockaddr *)&address, &size));
    port = ntohs(address.sin_port);
    close(fd);
}

static bool launch_server(const char *grace)
{
    char port_env[64];
    snprintf(port_env, sizeof port_env, "COLLAB_PORT=%d", port);
    char grace_env[64];
    snprintf(grace_env, sizeof grace_env, "COLLAB_HOST_GRACE_MS=%s", grace);
    const char *argv[] = {
        "env",
        "NODE_ENV=development",
        "COLLAB_HOST=127.0.0.1",
        port_env,
        "COLLAB_LOCAL_TOKEN_ISSUANCE=0",
        "COLLAB_ALLOWED_ORIGINS=",
        "COLLAB_PUBLIC_WS_URL=",
        grace_env,
        "COLLAB_ADMIN_TOKEN=0123456789abcdef0123456789abcdef",
        "COLLAB_TOKEN_SECRET=fedcba9876543210fedcba9876543210",
        "node",
        "tools/collab/server/server.mjs",
        NULL
    };
    if (!chibi_process_start(&server, argv, tool_root(), log_path)) {
        snprintf(startup_error, sizeof startup_error, "spawn: %s", strerror(errno));
        return false;
    }
    tool_own_process(server.pid);
    return true;
}

static bool wait_ready(uint64_t deadline)
{
    while (milliseconds() < deadline) {
        if (chibi_process_poll(&server)) {
            snprintf(
                startup_error,
                sizeof startup_error,
                "server exited with status %d before readiness",
                server.status
            );
            return false;
        }
        if (request("GET", "/healthz", NULL, "") == 200) {
            return true;
        }
        pause_poll();
    }
    snprintf(
        startup_error,
        sizeof startup_error,
        "health deadline expired; HTTP %d; %.160s",
        response_status,
        request_error
    );
    return false;
}

static bool start_server(const char *grace)
{
    return launch_server(grace) && wait_ready(milliseconds() + 5000);
}

static void teardown()
{
    yyjson_doc_free(response);
    response = NULL;
    if (server.pid > 0) {
        chibi_process_stop(&server);
        while (!chibi_process_poll(&server)) {
            pause_poll();
        }
        kill(-server.pid, SIGKILL);
        tool_release_process(server.pid);
        server.pid = 0;
    }
    if (passed) {
        tool_remove_tree(directory);
        return;
    }
    fprintf(stderr, "collab: %s\nserver log retained at %s:\n", startup_error, log_path);
    char *output = tool_read(log_path);
    if (output) {
        fputs(output, stderr);
        free(output);
    }
}

TEST_CASE(room_creation_and_invites_require_the_correct_authority)
{
    REQUIRE(start_server("30000"));
    REQUIRE(
        expect_status("POST", "/v1/rooms", "invalid-token-with-at-least-32-bytes", "{}", 401)
    );
    REQUIRE(expect_status("POST", "/v1/rooms", "0123456789abcdef0123456789abcdef", "{}", 201));
    REQUIRE(field("room") && field("token") && field("role"));
    REQUIRE(!strcmp(field("role"), "host"));
    char room[128], token[4096], invite_path[256];
    REQUIRE(strlen(field("room")) < sizeof room && strlen(field("token")) < sizeof token);
    strcpy(room, field("room"));
    strcpy(token, field("token"));
    snprintf(invite_path, sizeof invite_path, "/v1/rooms/%s/invites", room);
    REQUIRE(expect_status("POST", invite_path, token, "{\"role\":\"viewer\"}", 201));
    REQUIRE(field("room") && field("role") && field("token"));
    REQUIRE(!strcmp(field("room"), room) && !strcmp(field("role"), "viewer"));
    REQUIRE(strlen(field("token")) < sizeof token);
    strcpy(token, field("token"));
    REQUIRE(expect_status("POST", invite_path, token, "{\"role\":\"editor\"}", 403));
    REQUIRE(field("error") && !strcmp(field("error"), "forbidden"));
    passed = true;
    return true;
}

TEST_CASE(server_exit_fails_readiness_and_keeps_its_log)
{
    REQUIRE(!start_server("0"));
    REQUIRE(server.finished && server.status != 0);
    REQUIRE(strstr(startup_error, "server exited with status"));
    teardown();
    REQUIRE(!access(log_path, R_OK));
    char *output = tool_read(log_path);
    REQUIRE(output);
    bool has_error = strstr(output, "COLLAB_HOST_GRACE_MS must be a positive integer") !=
                     NULL;
    free(output);
    REQUIRE(has_error);
    passed = true;
    return true;
}

TEST_CASE(occupied_port_fails_readiness)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_port = htons(port),
                                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    assert(!bind(fd, (struct sockaddr *)&address, sizeof address));
    assert(!listen(fd, 1));
    bool ready = start_server("30000");
    close(fd);
    REQUIRE(!ready);
    REQUIRE(server.finished && server.status != 0);
    char *output = tool_read(log_path);
    REQUIRE(output);
    bool has_error = strstr(output, "EADDRINUSE") != NULL;
    free(output);
    REQUIRE(has_error);
    passed = true;
    return true;
}

TEST_CASE(live_process_without_health_fails_readiness)
{
    REQUIRE(launch_server("30000"));
    REQUIRE(!kill(-server.pid, SIGSTOP));
    bool ready = wait_ready(milliseconds() + 100);
    REQUIRE(!kill(-server.pid, SIGCONT));
    REQUIRE(!ready);
    REQUIRE(!chibi_process_poll(&server));
    REQUIRE(strstr(startup_error, "health deadline expired"));
    passed = true;
    return true;
}

TEST_CASE(request_reports_real_receive_and_connection_failures)
{
    REQUIRE(start_server("30000"));
    REQUIRE(!kill(-server.pid, SIGSTOP));
    int status = request("GET", "/healthz", NULL, "");
    int error = request_system_error;
    REQUIRE(!kill(-server.pid, SIGCONT));
    REQUIRE(status == 0);
    REQUIRE(error == EAGAIN || error == EWOULDBLOCK);
    REQUIRE(strstr(request_error, "recv:"));
    REQUIRE(!kill(-server.pid, SIGKILL));
    while (!chibi_process_poll(&server)) {
        pause_poll();
    }
    REQUIRE(request("GET", "/healthz", NULL, "") == 0);
    REQUIRE(request_system_error == ECONNREFUSED);
    REQUIRE(strstr(request_error, "connect:"));
    passed = true;
    return true;
}

void test_tool_collab()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(room_creation_and_invites_require_the_correct_authority);
    RUN_TEST_CASE(server_exit_fails_readiness_and_keeps_its_log);
    RUN_TEST_CASE(occupied_port_fails_readiness);
    RUN_TEST_CASE(live_process_without_health_fails_readiness);
    RUN_TEST_CASE(request_reports_real_receive_and_connection_failures);
    pu_setup(NULL, NULL);
}
