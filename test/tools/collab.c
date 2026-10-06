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
        close(fd);
        return 0;
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
            close(fd);
            return 0;
        }
        sent += (size_t)size;
    }
    while (received < sizeof incoming - 1) {
        ssize_t size = recv(fd, incoming + received, sizeof incoming - 1 - received, 0);
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size < 0) {
            close(fd);
            return 0;
        }
        if (!size) {
            break;
        }
        received += (size_t)size;
    }
    close(fd);
    incoming[received] = '\0';
    int status = 0;
    if (sscanf(incoming, "HTTP/1.1 %d", &status) != 1) {
        return 0;
    }
    char *json = strstr(incoming, "\r\n\r\n");
    if (json) {
        response = yyjson_read(json + 4, strlen(json + 4), 0);
    }
    return response ? status : 0;
}

static const char *field(const char *name)
{
    return yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(response), name));
}

static void setup()
{
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
    char port_env[64];
    snprintf(port_env, sizeof port_env, "COLLAB_PORT=%d", port);
    const char *argv[] = {
        "env",
        "NODE_ENV=development",
        "COLLAB_HOST=127.0.0.1",
        port_env,
        "COLLAB_LOCAL_TOKEN_ISSUANCE=0",
        "COLLAB_ALLOWED_ORIGINS=",
        "COLLAB_PUBLIC_WS_URL=",
        "COLLAB_HOST_GRACE_MS=30000",
        "COLLAB_ADMIN_TOKEN=0123456789abcdef0123456789abcdef",
        "COLLAB_TOKEN_SECRET=fedcba9876543210fedcba9876543210",
        "node",
        "tools/collab/server/server.mjs",
        NULL
    };
    server = (chibi_process){ 0 };
    if (!chibi_process_start(&server, argv, tool_root(), log_path)) {
        return;
    }
    tool_own_process(server.pid);
    uint64_t deadline = milliseconds() + 5000;
    while (milliseconds() < deadline && !chibi_process_poll(&server)) {
        if (request("GET", "/healthz", NULL, "") == 200) {
            return;
        }
        pause_poll();
    }
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
    }
    tool_remove_tree(directory);
}

TEST_CASE(room_creation_and_invites_require_the_correct_authority)
{
    REQUIRE(server.pid > 0 && !chibi_process_poll(&server));
    REQUIRE(request("POST", "/v1/rooms", "invalid-token-with-at-least-32-bytes", "{}") == 401);
    REQUIRE(request("POST", "/v1/rooms", "0123456789abcdef0123456789abcdef", "{}") == 201);
    REQUIRE(field("room") && field("token") && field("role"));
    REQUIRE(!strcmp(field("role"), "host"));
    char room[128], token[4096], invite_path[256];
    REQUIRE(strlen(field("room")) < sizeof room && strlen(field("token")) < sizeof token);
    strcpy(room, field("room"));
    strcpy(token, field("token"));
    snprintf(invite_path, sizeof invite_path, "/v1/rooms/%s/invites", room);
    REQUIRE(request("POST", invite_path, token, "{\"role\":\"viewer\"}") == 201);
    REQUIRE(field("room") && field("role") && field("token"));
    REQUIRE(!strcmp(field("room"), room) && !strcmp(field("role"), "viewer"));
    REQUIRE(strlen(field("token")) < sizeof token);
    strcpy(token, field("token"));
    REQUIRE(request("POST", invite_path, token, "{\"role\":\"editor\"}") == 403);
    REQUIRE(field("error") && !strcmp(field("error"), "forbidden"));
    return true;
}

void test_tool_collab()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(room_creation_and_invites_require_the_correct_authority);
    pu_setup(NULL, NULL);
}
