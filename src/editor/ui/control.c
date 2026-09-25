/* The editor command API over a private Unix socket (mode 0600). One client
 * at a time sends a JSON line; the editor answers on its frame thread and
 * closes the connection. */
#include "editor/ui/control.h"

#include "editor/commands/command.h"

#ifndef __EMSCRIPTEN__
#include "support/xalloc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define CONTROL_REQUEST_LIMIT 262144

static int listener = -1, client = -1;
static char address[sizeof(((struct sockaddr_un *)0)->sun_path)];
static char request[CONTROL_REQUEST_LIMIT];
static size_t received, sent;
static char *response;

static void disconnect_client()
{
    if (client >= 0) {
        close(client);
    }
    client = -1;
    received = sent = 0;
    free(response);
    response = NULL;
}

void editor_control_close()
{
    disconnect_client();
    if (listener >= 0) {
        close(listener);
        unlink(address);
    }
    listener = -1;
}

void editor_control_open()
{
    const char *path = getenv("CHIBI_EDITOR_SOCKET");
    if (!path || !path[0] || listener >= 0) {
        return;
    }
    if (strlen(path) >= sizeof address) {
        fprintf(stderr, "CHIBI_EDITOR_SOCKET path is too long\n");
        return;
    }
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    /* Never unlink another editor's socket. Its owner removes it on shutdown. */
    if (listener < 0 || bind(listener, (struct sockaddr *)&addr, sizeof addr)) {
        perror("editor control socket");
        if (listener >= 0) {
            close(listener);
        }
        listener = -1;
        return;
    }
    snprintf(address, sizeof address, "%s", path);
    if (chmod(path, 0600) || listen(listener, 1)) {
        perror("editor control socket");
        editor_control_close();
        return;
    }
    fcntl(listener, F_SETFL, O_NONBLOCK);
    fcntl(listener, F_SETFD, FD_CLOEXEC);
}

static void respond(char *json)
{
    size_t length = strlen(json);
    response = xmalloc(length + 2);
    memcpy(response, json, length);
    memcpy(response + length, "\n", 2);
    free(json);
}

static bool accept_client()
{
    client = accept(listener, NULL, NULL);
    if (client < 0) {
        return false;
    }
    fcntl(client, F_SETFL, O_NONBLOCK);
    fcntl(client, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int yes = 1;
    setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#endif
    return true;
}

static void send_response()
{
    ssize_t count = write(client, response + sent, strlen(response) - sent);
    if (count > 0) {
        sent += (size_t)count;
        if (!response[sent]) {
            disconnect_client();
        }
        return;
    }
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        disconnect_client();
    }
}

/* Reads until a complete line; true once the request is ready. */
static bool read_request()
{
    ssize_t count = read(client, request + received, sizeof request - received - 1);
    if (count <= 0) {
        if (!count || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            disconnect_client();
        }
        return false;
    }
    received += (size_t)count;
    request[received] = 0;
    if (strchr(request, '\n')) {
        return true;
    }
    if (received == sizeof request - 1) {
        respond(xstrdup("{\"ok\":false,\"status\":\"failed\",\"message\":\"Request too large\"}"));
    }
    return false;
}

void editor_control_frame(app_scene_authoring *scene)
{
    if (listener < 0 || (client < 0 && !accept_client())) {
        return;
    }
    if (response) {
        send_response();
        return;
    }
    if (read_request()) {
        *strchr(request, '\n') = 0;
        respond(editor_commands_api(scene, request));
    }
}
#else
void editor_control_open() {}

void editor_control_close() {}

void editor_control_frame(app_scene_authoring *) {}
#endif
