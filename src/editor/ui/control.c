#include "editor/ui/control.h"
#include "editor/commands/command.h"

#include "editor/ui/interaction.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor.h"
#include "support/xalloc.h"

#ifndef __EMSCRIPTEN__
#include <errno.h>
#include <fcntl.h>
#include <imgui/dcimgui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <yyjson.h>

static int listener = -1, client = -1;
static editor_interaction *pending_input;
static char address[sizeof(((struct sockaddr_un *)0)->sun_path)];
static char request[262144];
static char instance[48];
static size_t received, sent;
static char *response, *previous;
static int revision;

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
    editor_interaction_free(pending_input);
    pending_input = NULL;
    disconnect_client();
    if (listener >= 0) {
        close(listener);
        unlink(address);
    }
    listener = -1;
    free(previous);
    previous = NULL;
    revision = 0;
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
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    snprintf(instance, sizeof instance, "%lld-%ld", (long long)now.tv_sec, now.tv_nsec);
}

static yyjson_mut_val *observe(yyjson_mut_doc *doc, editor *graph, app_scene_authoring *scene)
{
    yyjson_mut_val *state = yyjson_mut_obj(doc);
    app_editor_document_context document = app_scene_authoring_document(scene);
    editor_query query;
    editor_query_state(graph, &query);
    yyjson_mut_obj_add_str(doc, state, "document", document.id ? document.id : "");
    yyjson_mut_obj_add_str(doc, state, "content_root", app_scene_authoring_content_root(scene));
    yyjson_mut_obj_add_bool(doc, state, "graph_dirty", query.dirty);
    yyjson_mut_obj_add_str(doc, state, "message", app_scene_authoring_message(scene));
    char *snapshot = editor_interaction_snapshot();
    yyjson_doc *ui = yyjson_read(snapshot, strlen(snapshot), 0);
    yyjson_val *root = yyjson_doc_get_root(ui);
    yyjson_mut_obj_add_int(doc, state, "popup_depth", yyjson_get_int(yyjson_obj_get(root, "popup_depth")));
    yyjson_mut_obj_add_val(
        doc,
        state,
        "controls",
        yyjson_val_mut_copy(doc, yyjson_obj_get(root, "controls"))
    );
    yyjson_doc_free(ui);
    free(snapshot);
    return state;
}

static void respond(editor *graph, app_scene_authoring *scene, const char *error)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "contract", 1);
    yyjson_mut_obj_add_bool(doc, root, "ok", error == NULL);
    yyjson_mut_obj_add_int(doc, root, "pid", getpid());
    yyjson_mut_obj_add_str(doc, root, "instance", instance);
    yyjson_mut_obj_add_int(doc, root, "revision", revision);
    yyjson_mut_obj_add_val(doc, root, "state", observe(doc, graph, scene));
    if (error) {
        yyjson_mut_obj_add_str(doc, root, "error", error);
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    size_t length = strlen(json);
    response = xmalloc(length + 2);
    memcpy(response, json, length);
    response[length] = '\n';
    response[length + 1] = 0;
    free(json);
    yyjson_mut_doc_free(doc);
}

static const char *accept_action(yyjson_val *root)
{
    const char *operation = yyjson_get_str(yyjson_obj_get(root, "operation"));
    const char *observed_instance = yyjson_get_str(yyjson_obj_get(root, "instance"));
    if (!observed_instance || strcmp(observed_instance, instance) ||
        yyjson_get_int(yyjson_obj_get(root, "revision")) != revision ||
        yyjson_get_int(yyjson_obj_get(root, "pid")) != getpid()) {
        return "Stale observation";
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *action = yyjson_val_mut_copy(doc, root);
    yyjson_mut_doc_set_root(doc, action);
    yyjson_mut_obj_add_str(doc, action, "action", operation);
    char *json = yyjson_mut_write(doc, 0, NULL);
    char *observation = editor_interaction_snapshot();
    static char error[256];
    pending_input = editor_interaction_start(observation, json, error, sizeof error);
    free(observation);
    free(json);
    yyjson_mut_doc_free(doc);
    return pending_input ? NULL : error;
}

void editor_control_frame(editor *graph, app_scene_authoring *scene)
{
    if (listener < 0) {
        return;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(doc, observe(doc, graph, scene));
    char *json = yyjson_mut_write(doc, 0, NULL);
    if (!previous || strcmp(previous, json)) {
        revision++;
        free(previous);
        previous = json;
    } else {
        free(json);
    }
    yyjson_mut_doc_free(doc);
    if (pending_input) {
        int result = editor_interaction_frame(pending_input);
        if (result) {
            editor_interaction_free(pending_input);
            pending_input = NULL;
            respond(graph, scene, result < 0 ? "Control changed or text readback failed" : NULL);
        }
    }
    if (client < 0) {
        client = accept(listener, NULL, NULL);
        if (client < 0) {
            return;
        }
        fcntl(client, F_SETFL, O_NONBLOCK);
        fcntl(client, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
        int yes = 1;
        setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#endif
    }
    if (response) {
        ssize_t count = write(client, response + sent, strlen(response) - sent);
        if (count > 0) {
            sent += count;
            if (!response[sent]) {
                disconnect_client();
            }
        } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            disconnect_client();
        }
        return;
    }
    if (pending_input) {
        return;
    }
    ssize_t count = read(client, request + received, sizeof request - received - 1);
    if (count <= 0) {
        if (!count || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            disconnect_client();
        }
        return;
    }
    received += count;
    request[received] = 0;
    if (!strchr(request, '\n')) {
        if (received == sizeof request - 1) {
            respond(graph, scene, "Request too large");
        }
        return;
    }
    yyjson_doc *input = yyjson_read(request, received, 0);
    yyjson_val *root = input ? yyjson_doc_get_root(input) : NULL;
    const char *operation = yyjson_get_str(yyjson_obj_get(root, "operation"));
    const char *error = NULL;
    if (!operation) {
        error = "Invalid request";
    } else if (!strcmp(operation, "inspect") || !strcmp(operation, "submit") ||
               !strcmp(operation, "resolve_objects") || !strcmp(operation, "status") ||
               !strcmp(operation, "respond") || !strcmp(operation, "cancel")) {
        char *json = editor_commands_api(scene, request);
        size_t length = strlen(json);
        response = xmalloc(length + 2);
        memcpy(response, json, length);
        response[length] = '\n';
        response[length + 1] = 0;
        free(json);
        yyjson_doc_free(input);
        return;
    } else if (strcmp(operation, "snapshot")) {
        error = accept_action(root);
    }
    if (!pending_input) {
        respond(graph, scene, error);
    }
    yyjson_doc_free(input);
}
#else
void editor_control_open() {}
void editor_control_close() {}
void editor_control_frame(editor *, app_scene_authoring *) {}
#endif
