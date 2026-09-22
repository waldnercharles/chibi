#include "editor/ui/command_transport.h"

#include "support/xalloc.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

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
#endif

struct editor_command_request
{
    char *input;
    size_t sent, received;
    char output[262144];
    bool complete;
    Uint64 started;
#ifdef __EMSCRIPTEN__
    int id;
#else
    SDL_Process *process;
#endif
};

#ifndef __EMSCRIPTEN__
static SDL_Process *worker;
#endif

editor_command_request *editor_command_request_start(const char *json)
{
    editor_command_request *request = xcalloc(1, sizeof *request);
    request->started = SDL_GetTicks();
#ifdef __EMSCRIPTEN__
    request->id = web_start(json);
    if (!request->id) {
        free(request);
        return NULL;
    }
#else
    const char *python = getenv("CHIBI_PYTHON");
    const char *args[] = { python ? python : "python3", CHIBI_COMMAND_WORKER, NULL };
    if (worker && SDL_WaitProcess(worker, false, NULL)) {
        SDL_DestroyProcess(worker);
        worker = NULL;
    }
    if (!worker) {
        worker = SDL_CreateProcess(args, true);
    }
    request->process = worker;
    if (!request->process) {
        free(request);
        return NULL;
    }
    size_t length = strlen(json);
    request->input = xmalloc(length + 2);
    memcpy(request->input, json, length);
    request->input[length] = '\n';
    request->input[length + 1] = 0;
#endif
    return request;
}

char *editor_command_request_poll(editor_command_request *request)
{
    if (SDL_GetTicks() - request->started > 30000) {
        return xstrdup("{\"ok\":false,\"error\":\"Command model timed out\"}");
    }
#ifdef __EMSCRIPTEN__
    return web_poll(request->id);
#else
    if (request->input) {
        SDL_IOStream *input = SDL_GetProcessInput(request->process);
        request->sent += SDL_WriteIO(
            input,
            request->input + request->sent,
            strlen(request->input) - request->sent
        );
        if (SDL_GetIOStatus(input) == SDL_IO_STATUS_ERROR) {
            return xstrdup("{\"ok\":false,\"error\":\"Could not send command to local worker\"}");
        }
        if (!request->input[request->sent]) {
            free(request->input);
            request->input = NULL;
        }
    }
    SDL_IOStream *output = SDL_GetProcessOutput(request->process);
    request->received += SDL_ReadIO(
        output,
        request->output + request->received,
        sizeof request->output - request->received - 1
    );
    request->output[request->received] = 0;
    if (strchr(request->output, '\n')) {
        request->complete = true;
        return xstrdup(request->output);
    }
    if (request->received == sizeof request->output - 1) {
        return xstrdup("{\"ok\":false,\"error\":\"Command response too large\"}");
    }
    if (SDL_WaitProcess(request->process, false, NULL)) {
        return xstrdup("{\"ok\":false,\"error\":\"Local command worker stopped\"}");
    }
    return NULL;
#endif
}

void editor_command_request_free(editor_command_request *request)
{
    if (!request) {
        return;
    }
#ifdef __EMSCRIPTEN__
    web_cancel(request->id);
#else
    if (!request->complete) {
        if (!SDL_WaitProcess(request->process, false, NULL)) {
            SDL_KillProcess(request->process, true);
            SDL_WaitProcess(request->process, true, NULL);
        }
        SDL_DestroyProcess(request->process);
        worker = NULL;
    }
#endif
    free(request->input);
    free(request);
}
