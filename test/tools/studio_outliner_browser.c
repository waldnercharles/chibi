/* One opt-in rendered Studio check. The C fixture owns the browser, input,
 * limits and assertions. JavaScript only gates animation frames and transports
 * existing browser/Studio observations. No product hooks or sandbox overrides.
 *
 * Admission: one serial tab, 1280x720 at DPR 1, exactly 16 application frames,
 * three PNGs, 64 HTTP requests, 96 CDP commands, 2048 CDP messages, 55 seconds
 * inside the production 60-second supervisor. The checked-in Studio scene has
 * 43 actors; its first three are Settings, Data and Theme. Screenshots require
 * manual review: initial Studio/43 and those rows; Settings/1 after filtering;
 * then 0 and No objects match “__outliner_missing__”. This is not a DOM or OCR
 * assertion of the RmlUi tree, nor a whole-Studio/browser performance test.
 */
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
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <yyjson.h>

enum
{
    BROWSER_SECONDS = 55,
    HTTP_LIMIT = 64,
    COMMAND_LIMIT = 96,
    MESSAGE_LIMIT = 2048,
    FILE_LIMIT = 64 * 1024 * 1024,
    SOURCE_LIMIT = 128 * 1024,
    MESSAGE_BYTES = 8 * 1024 * 1024,
    LOG_BYTES = 64 * 1024,
    PIXELS = 64 * 36 * 4,
    FRAME_LIMIT = 16,
    WIDTH = 1280,
    HEIGHT = 720,
};

static struct
{
    int server, input, output, diagnostic;
    chibi_process process;
    bool owned, temporary_owned, failed, runtime_error, loaded;
    double deadline, stop_deadline;
    char temporary[PATH_MAX], root[PATH_MAX], artifacts[PATH_MAX];
    char session[256], log[LOG_BYTES + 1];
    size_t log_size, used;
    char *wire;
    int requests, commands, messages;
    yyjson_doc *reply;
    unsigned char *png;
    size_t png_size;
    unsigned char prior_pixels[PIXELS];
    void (*previous_pipe)(int);
} browser;

extern char **environ;

/* The gate observes the already-public frame log. It does not call handlers,
 * inspect Wasm memory, replace source, mutate the UI model or synthesize input.
 * Capturing a tiny canvas sample before the animation callback returns avoids
 * reading a WebGL drawing buffer after the compositor has discarded it. */
static const char gate[] =
    "(()=>{const native=requestAnimationFrame.bind(window);"
    "const pending=new Map();let serial=0,scheduled=false,target=4,resolve;"
    "let pixels=[];const frames=()=>typeof "
    "window.Module?._chibi_frame_log_csv==='function'"
    "?Module.chibiFrames().length:0;"
    "const state=()=>({frames:frames(),width:Module.canvas.width,"
    "height:Module.canvas.height,error:!!document.querySelector("
    "'#status[data-error],#status[data-storage-error]'),"
    "status:document.querySelector('#status')"
    "?."
    "textContent||'',pixels});"
    "const sample=()=>{if(frames()===16)return;"
    "const c=document.createElement('canvas');c.width=64;c.height=36;"
    "const g=c.getContext('2d');g.drawImage(Module.canvas,0,0,64,36);"
    "pixels=Array.from(g.getImageData(0,0,64,36).data);};"
    "const schedule=()=>{if(scheduled||!pending.size||frames()>=target)return;"
    "scheduled=true;native(time=>{scheduled=false;"
    "for(const [id,callback] of Array.from(pending)){"
    "if(frames()>=target)break;pending.delete(id);callback(time);"
    "if(frames()>target)throw Error('frame gate overshoot');}"
    "if(frames()===target){sample();if(resolve){const done=resolve;"
    "resolve=undefined;done(state());}}schedule();});};"
    "window.requestAnimationFrame=callback=>{"
    "if(pending.size>=32)throw Error('frame callback limit');"
    "const id=++serial;pending.set(id,callback);schedule();return id;};"
    "window.cancelAnimationFrame=id=>pending.delete(id);"
    "window.__outlinerBrowser={advance:value=>{"
    "if(resolve||value<frames()||![4,5,6,8,9,12,16].includes(value))"
    "throw Error('invalid finite frame request');target=value;"
    "if(frames()===target)return Promise.resolve(state());"
    "return new Promise(done=>{resolve=done;schedule();});}};})();";

static bool fail(const char *message)
{
    fprintf(stderr, "Studio browser: %s\n", message);
    browser.failed = true;
    return false;
}

static void close_fd(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static bool live()
{
    if (browser.failed) {
        return false;
    }
    if (chibi_process_now() >= browser.deadline) {
        return fail("55-second browser deadline reached");
    }
    if (browser.owned && chibi_process_poll(&browser.process)) {
        return fail("browser exited before the finite check completed");
    }
    return true;
}

static bool append_log(const char *text, size_t length)
{
    size_t available = LOG_BYTES - browser.log_size;
    size_t copied = length < available ? length : available;
    memcpy(browser.log + browser.log_size, text, copied);
    browser.log_size += copied;
    browser.log[browser.log_size] = 0;
    return copied == length || fail("64 KiB diagnostic limit reached");
}

static bool write_bytes(int fd, const void *data, size_t size)
{
    const char *bytes = data;
    while (size && live()) {
        ssize_t count = write(fd, bytes, size);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd ready = { .fd = fd, .events = POLLOUT };
            poll(&ready, 1, 10);
            continue;
        }
        if (count <= 0) {
            return fail("transport write failed");
        }
        bytes += count;
        size -= (size_t)count;
    }
    return !size;
}

static bool serve()
{
    if (++browser.requests > HTTP_LIMIT) {
        return fail("HTTP request limit reached");
    }
    int client = accept(browser.server, NULL, NULL);
    if (client < 0) {
        return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK ||
               fail("HTTP accept failed");
    }
    struct timeval timeout = { .tv_sec = 1 };
    /* accept() does not inherit O_NONBLOCK consistently across platforms. */
    if (fcntl(client, F_SETFL, 0) ||
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) ||
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout)) {
        close(client);
        return fail("HTTP socket timeout setup failed");
    }
    char request[8192] = { 0 };
    size_t used = 0;
    while (used < sizeof request - 1 && live()) {
        ssize_t got = read(client, request + used, sizeof request - used - 1);
        if (got <= 0) {
            close(client);
            return true; /* Speculative connection, not another admission. */
        }
        used += (size_t)got;
        request[used] = 0;
        if (strstr(request, "\r\n\r\n")) {
            break;
        }
    }
    char path[256];
    if (!strstr(request, "\r\n\r\n") ||
        sscanf(request, "GET %255s HTTP/1.1", path) != 1) {
        close(client);
        return fail("unsupported HTTP request");
    }
    char *query = strchr(path, '?');
    if (query) {
        *query = 0;
    }
    static const char *const files[] = { "chibi-studio.html",
                                         "chibi-studio.js",
                                         "chibi-studio.wasm",
                                         "chibi-studio.data",
                                         "chibi-default.js" };
    static const char *const types[] = { "text/html",
                                         "text/javascript",
                                         "application/wasm",
                                         "application/octet-stream",
                                         "text/javascript" };
    int selected = -1;
    for (int i = 0; i < 5; i++) {
        if (path[0] == '/' && !strcmp(path + 1, files[i])) {
            selected = i;
        }
    }
    if (selected < 0) {
        static const char
            missing[] = "HTTP/1.1 404 Not Found\r\nContent-Length: "
                        "0\r\nConnection: close\r\n\r\n";
        bool sent = write_bytes(client, missing, sizeof missing - 1);
        close(client);
        return sent;
    }
    char filename[PATH_MAX];
    int length = snprintf(filename, sizeof filename, "%s/%s", browser.root, files[selected]);
    int fd = length > 0 && length < (int)sizeof filename
               ? open(filename, O_RDONLY | O_NOFOLLOW | O_CLOEXEC)
               : -1;
    struct stat info;
    if (fd < 0 || fstat(fd, &info) || !S_ISREG(info.st_mode) ||
        info.st_size <= 0 || info.st_size > FILE_LIMIT) {
        if (fd >= 0) {
            close(fd);
        }
        close(client);
        return fail("missing, oversized or nonregular web artifact");
    }
    char header[512];
    length = snprintf(
        header,
        sizeof header,
        "HTTP/1.1 200 OK\r\nContent-Length: %lld\r\nContent-Type: %s\r\n"
        "Cross-Origin-Opener-Policy: same-origin\r\n"
        "Cross-Origin-Embedder-Policy: require-corp\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        (long long)info.st_size,
        types[selected]
    );
    bool ok = length > 0 && length < (int)sizeof header &&
              write_bytes(client, header, (size_t)length);
    char bytes[65536];
    ssize_t count = 0;
    while (ok && (count = read(fd, bytes, sizeof bytes)) > 0) {
        ok = write_bytes(client, bytes, (size_t)count);
    }
    close(fd);
    close(client);
    return ok && count == 0;
}

static bool pump()
{
    struct pollfd fds[] = {
        { .fd = browser.output, .events = POLLIN },
        { .fd = browser.diagnostic, .events = POLLIN },
        { .fd = browser.server, .events = POLLIN },
    };
    if (!live()) {
        return false;
    }
    int ready = poll(fds, 3, 10);
    if (ready < 0) {
        return errno == EINTR || fail("transport poll failed");
    }
    if (fds[0].revents & (POLLIN | POLLHUP)) {
        if (browser.used == MESSAGE_BYTES) {
            return fail("CDP message byte limit reached");
        }
        ssize_t count = read(
            browser.output,
            browser.wire + browser.used,
            MESSAGE_BYTES - browser.used
        );
        if (count <= 0) {
            return count < 0 && (errno == EAGAIN || errno == EINTR)
                     ? true
                     : fail("CDP pipe closed");
        }
        browser.used += (size_t)count;
    }
    if (fds[1].revents & (POLLIN | POLLHUP)) {
        char text[4096];
        ssize_t count = read(browser.diagnostic, text, sizeof text);
        if (count > 0 && !append_log(text, (size_t)count)) {
            return false;
        }
        if (!count) {
            close_fd(&browser.diagnostic);
        }
    }
    return !(fds[2].revents & POLLIN) || serve();
}

static yyjson_doc *message()
{
    while (live()) {
        char *end = memchr(browser.wire, 0, browser.used);
        if (!end) {
            if (!pump()) {
                return NULL;
            }
            continue;
        }
        if (++browser.messages > MESSAGE_LIMIT) {
            fail("CDP message count limit reached");
            return NULL;
        }
        size_t length = (size_t)(end - browser.wire);
        yyjson_doc *doc = yyjson_read(browser.wire, length, 0);
        memmove(browser.wire, end + 1, browser.used - length - 1);
        browser.used -= length + 1;
        if (!doc) {
            fail("malformed CDP JSON");
        }
        return doc;
    }
    return NULL;
}

static void event(yyjson_doc *doc)
{
    yyjson_val *value = yyjson_doc_get_root(doc);
    const char *name = yyjson_get_str(yyjson_obj_get(value, "method"));
    yyjson_val *body = yyjson_obj_get(value, "params");
    const char *kind = yyjson_get_str(yyjson_obj_get(body, "type"));
    if (name && !strcmp(name, "Page.loadEventFired")) {
        browser.loaded = true;
    }
    if (name && (!strcmp(name, "Runtime.exceptionThrown") ||
                 (!strcmp(name, "Runtime.consoleAPICalled") && kind &&
                  !strcmp(kind, "error")))) {
        browser.runtime_error = true;
        size_t length;
        char *detail = yyjson_write(doc, 0, &length);
        if (detail) {
            append_log(detail, length);
            append_log("\n", 1);
            free(detail);
        }
    }
}

static yyjson_val *command(const char *method, yyjson_mut_doc *params)
{
    yyjson_doc_free(browser.reply);
    browser.reply = NULL;
    if (++browser.commands > COMMAND_LIMIT) {
        fail("CDP command limit reached");
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "id", browser.commands);
    yyjson_mut_obj_add_str(doc, root, "method", method);
    if (browser.session[0]) {
        yyjson_mut_obj_add_str(doc, root, "sessionId", browser.session);
    }
    if (params) {
        yyjson_mut_obj_add_val(doc, root, "params", yyjson_mut_doc_get_root(params));
    }
    size_t length;
    char *json = yyjson_mut_write(doc, 0, &length);
    bool sent = json && write_bytes(browser.input, json, length + 1);
    free(json);
    yyjson_mut_doc_free(doc);
    if (!sent) {
        return NULL;
    }
    while ((browser.reply = message())) {
        yyjson_val *value = yyjson_doc_get_root(browser.reply);
        yyjson_val *id = yyjson_obj_get(value, "id");
        if (yyjson_is_int(id)) {
            if (yyjson_get_int(id) != browser.commands ||
                yyjson_obj_get(value, "error")) {
                char *detail = yyjson_write(browser.reply, 0, NULL);
                fprintf(stderr, "Studio browser CDP %s: %s\n", method, detail ? detail : "invalid reply");
                free(detail);
                fail("CDP command rejected");
                return NULL;
            }
            return yyjson_obj_get(value, "result");
        }
        event(browser.reply);
        yyjson_doc_free(browser.reply);
        browser.reply = NULL;
    }
    return NULL;
}

static bool loaded()
{
    while (!browser.loaded && live()) {
        yyjson_doc *doc = message();
        if (!doc) {
            return false;
        }
        event(doc);
        yyjson_doc_free(doc);
    }
    return browser.loaded && !browser.runtime_error;
}

static yyjson_val *call(const char *method, const char *json)
{
    yyjson_doc *source = yyjson_read(json, strlen(json), 0);
    assert(source);
    yyjson_mut_doc *params = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(params, yyjson_val_mut_copy(params, yyjson_doc_get_root(source)));
    yyjson_val *result = command(method, params);
    yyjson_mut_doc_free(params);
    yyjson_doc_free(source);
    return result;
}

static yyjson_val *text_command(const char *method, const char *key, const char *text)
{
    yyjson_mut_doc *params = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(params);
    yyjson_mut_doc_set_root(params, root);
    yyjson_mut_obj_add_str(params, root, key, text);
    yyjson_val *result = command(method, params);
    yyjson_mut_doc_free(params);
    return result;
}

static yyjson_val *evaluate(const char *expression)
{
    yyjson_mut_doc *params = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(params);
    yyjson_mut_doc_set_root(params, root);
    yyjson_mut_obj_add_str(params, root, "expression", expression);
    yyjson_mut_obj_add_bool(params, root, "awaitPromise", true);
    yyjson_mut_obj_add_bool(params, root, "returnByValue", true);
    yyjson_val *result = command("Runtime.evaluate", params);
    yyjson_mut_doc_free(params);
    if (!result || yyjson_obj_get(result, "exceptionDetails")) {
        fail("browser observation failed");
        return NULL;
    }
    return yyjson_obj_get(yyjson_obj_get(result, "result"), "value");
}

/* SDL's Emscripten backend reads keypress.charCode, not IME input events.
 * These are trusted browser key events; no DOM dispatchEvent or SDL injection. */
static bool type_text(const char *text)
{
    size_t length = strlen(text);
    REQUIRE(length <= 19);
    for (size_t i = 0; i < length; i++) {
        REQUIRE(
            (text[i] >= 'A' && text[i] <= 'Z') ||
            (text[i] >= 'a' && text[i] <= 'z') || text[i] == '_'
        );
        char params[128];
        snprintf(
            params,
            sizeof params,
            "{\"type\":\"char\",\"text\":\"%c\",\"unmodifiedText\":\"%c\","
            "\"key\":\"%c\"}",
            text[i],
            text[i],
            text[i]
        );
        REQUIRE(call("Input.dispatchKeyEvent", params));
    }
    return true;
}

/* The descriptor remap stays private to this fixture. There is no shell,
 * debugging TCP port, inherited listener, or change to product process APIs. */
static bool pipe_pair(int pair[2])
{
    int original[2];
    if (pipe(original)) {
        return false;
    }
    pair[0] = fcntl(original[0], F_DUPFD_CLOEXEC, 10);
    pair[1] = fcntl(original[1], F_DUPFD_CLOEXEC, 10);
    close(original[0]);
    close(original[1]);
    if (pair[0] < 0 || pair[1] < 0) {
        close_fd(&pair[0]);
        close_fd(&pair[1]);
        return false;
    }
    return true;
}

static bool start()
{
    const char *executable = getenv("CHIBI_TEST_BROWSER");
    if (!executable) {
#ifdef __APPLE__
        executable = "/Applications/Google Chrome.app/Contents/MacOS/Google "
                     "Chrome";
#else
        executable = "/usr/bin/chromium";
#endif
    }
    if (executable[0] != '/' || access(executable, X_OK)) {
        return fail("CHIBI_TEST_BROWSER must name an executable absolute path");
    }
    int to[2] = { -1, -1 }, from[2] = { -1, -1 }, log[2] = { -1, -1 };
    if (!pipe_pair(to) || !pipe_pair(from) || !pipe_pair(log)) {
        close_fd(&to[0]);
        close_fd(&to[1]);
        close_fd(&from[0]);
        close_fd(&from[1]);
        close_fd(&log[0]);
        close_fd(&log[1]);
        return fail("browser pipe creation failed");
    }
    char profile[PATH_MAX + 32];
    snprintf(profile, sizeof profile, "--user-data-dir=%s/profile", browser.temporary);
    const char *argv[] = { executable,
                           "--headless=new",
                           "--remote-debugging-pipe",
                           "--window-size=1280,720",
                           "--force-device-scale-factor=1",
                           "--no-first-run",
                           "--no-default-browser-check",
                           "--disable-background-networking",
                           "--disable-background-timer-throttling",
                           "--disable-renderer-backgrounding",
                           "--enable-logging=stderr",
                           profile,
                           "--no-startup-window",
                           NULL };
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int error = posix_spawn_file_actions_init(&actions);
    bool actions_ready = !error, attributes_ready = false;
    if (!error) {
        error = posix_spawnattr_init(&attributes);
        attributes_ready = !error;
    }
    if (!error) {
        error = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    }
    if (!error) {
        error = posix_spawn_file_actions_adddup2(&actions, log[1], 1);
    }
    if (!error) {
        error = posix_spawn_file_actions_adddup2(&actions, log[1], 2);
    }
    if (!error) {
        error = posix_spawn_file_actions_adddup2(&actions, to[0], 3);
    }
    if (!error) {
        error = posix_spawn_file_actions_adddup2(&actions, from[1], 4);
    }
    if (!error) {
        error = posix_spawnattr_setpgroup(&attributes, 0);
    }
    sigset_t signals;
    sigemptyset(&signals);
    if (!error) {
        error = posix_spawnattr_setsigmask(&attributes, &signals);
    }
    sigaddset(&signals, SIGPIPE);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    if (!error) {
        error = posix_spawnattr_setsigdefault(&attributes, &signals);
    }
    if (!error) {
        error = posix_spawnattr_setflags(
            &attributes,
            POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF
        );
    }
    if (!error) {
        error = posix_spawn(
            &browser.process.pid,
            executable,
            &actions,
            &attributes,
            (char *const *)argv,
            environ
        );
    }
    if (attributes_ready) {
        posix_spawnattr_destroy(&attributes);
    }
    if (actions_ready) {
        posix_spawn_file_actions_destroy(&actions);
    }
    close_fd(&to[0]);
    close_fd(&from[1]);
    close_fd(&log[1]);
    if (error) {
        close_fd(&to[1]);
        close_fd(&from[0]);
        close_fd(&log[0]);
        return fail(strerror(error));
    }
    browser.input = to[1];
    browser.output = from[0];
    browser.diagnostic = log[0];
    browser.owned = true;
    tool_own_process(browser.process.pid);
    return !fcntl(browser.input, F_SETFL, O_NONBLOCK) &&
           !fcntl(browser.output, F_SETFL, O_NONBLOCK) &&
           !fcntl(browser.diagnostic, F_SETFL, O_NONBLOCK);
}

static bool artifact(const char *name, const void *bytes, size_t size)
{
    char path[PATH_MAX];
    int length = snprintf(path, sizeof path, "%s/%s", browser.artifacts, name);
    if (length <= 0 || length >= (int)sizeof path) {
        return false;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        return false;
    }
    const char *data = bytes;
    bool ok = true;
    while (size) {
        ssize_t count = write(fd, data, size);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            ok = false;
            break;
        }
        size -= (size_t)count;
        data += count;
    }
    return !close(fd) && ok;
}

static int base64(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

static bool screenshot(const char *name)
{
    yyjson_val *result = call("Page.captureScreenshot", "{\"format\":\"png\",\"fromSurface\":true,\"captureBeyondViewport\":false}");
    const char *encoded = yyjson_get_str(yyjson_obj_get(result, "data"));
    if (!encoded) {
        return false;
    }
    size_t size = strlen(encoded);
    if (!size || size % 4 || size > MESSAGE_BYTES - 1024) {
        return false;
    }
    free(browser.png);
    browser.png = malloc(size / 4 * 3);
    assert(browser.png);
    browser.png_size = 0;
    for (size_t i = 0; i < size; i += 4) {
        int a = base64((unsigned char)encoded[i]);
        int b = base64((unsigned char)encoded[i + 1]);
        int c = encoded[i + 2] == '=' ? 0 : base64((unsigned char)encoded[i + 2]);
        int d = encoded[i + 3] == '=' ? 0 : base64((unsigned char)encoded[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 ||
            (encoded[i + 2] == '=' && encoded[i + 3] != '=') ||
            ((encoded[i + 2] == '=' || encoded[i + 3] == '=') && i + 4 != size)) {
            return false;
        }
        unsigned value = (unsigned)a << 18 | (unsigned)b << 12 |
                         (unsigned)c << 6 | (unsigned)d;
        browser.png[browser.png_size++] = (unsigned char)(value >> 16);
        if (encoded[i + 2] != '=') {
            browser.png[browser.png_size++] = (unsigned char)(value >> 8);
        }
        if (encoded[i + 3] != '=') {
            browser.png[browser.png_size++] = (unsigned char)value;
        }
    }
    static const unsigned char header[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (browser.png_size < 24 || memcmp(browser.png, header, sizeof header) ||
        memcmp(browser.png + 12, "IHDR", 4)) {
        return false;
    }
    unsigned width = (unsigned)browser.png[16] << 24 |
                     (unsigned)browser.png[17] << 16 |
                     (unsigned)browser.png[18] << 8 | browser.png[19];
    unsigned height = (unsigned)browser.png[20] << 24 |
                      (unsigned)browser.png[21] << 16 |
                      (unsigned)browser.png[22] << 8 | browser.png[23];
    return width == WIDTH && height == HEIGHT &&
           artifact(name, browser.png, browser.png_size);
}

static bool advance(int frame, bool sample, bool changed)
{
    char expression[96];
    snprintf(expression, sizeof expression, "window.__outlinerBrowser.advance(%d)", frame);
    yyjson_val *state = evaluate(expression);
    REQUIRE(state && !browser.runtime_error);
    REQUIRE(yyjson_get_int(yyjson_obj_get(state, "frames")) == frame);
    REQUIRE(yyjson_get_int(yyjson_obj_get(state, "width")) == WIDTH);
    REQUIRE(yyjson_get_int(yyjson_obj_get(state, "height")) == HEIGHT);
    REQUIRE(!yyjson_get_bool(yyjson_obj_get(state, "error")));
    if (!sample) {
        return true;
    }
    yyjson_val *pixels = yyjson_obj_get(state, "pixels");
    REQUIRE(yyjson_arr_size(pixels) == PIXELS);
    unsigned char current[PIXELS];
    int minimum = 255, maximum = 0;
    for (int i = 0; i < PIXELS; i++) {
        yyjson_val *value = yyjson_arr_get(pixels, (size_t)i);
        REQUIRE(
            yyjson_is_int(value) && yyjson_get_int(value) >= 0 &&
            yyjson_get_int(value) <= 255
        );
        current[i] = (unsigned char)yyjson_get_int(value);
        if (i % 4 != 3) {
            if (current[i] < minimum) {
                minimum = current[i];
            }
            if (current[i] > maximum) {
                maximum = current[i];
            }
        }
    }
    REQUIRE(maximum - minimum > 16);
    REQUIRE(!changed || memcmp(current, browser.prior_pixels, sizeof current));
    memcpy(browser.prior_pixels, current, sizeof current);
    return true;
}

static bool sources()
{
    static const char *const names[] = {
        "project.kdl",
        "content/scenes/Studio.kdl",
        "content/libraries/OutlinerRowPolicies.chibi",
        "content/behaviors/OutlinerRows.chibi"
    };
    const char *source = getenv("CHIBI_STUDIO_SOURCE");
    REQUIRE(source && source[0] == '/');
    yyjson_val *values = evaluate(
        "['project.kdl','content/scenes/Studio.kdl','content/libraries/"
        "OutlinerRowPolicies.chibi',"
        "'content/behaviors/"
        "OutlinerRows.chibi'].map(name=>Module.FS.readFile('/projects/studio/"
        "'+name,{encoding:'utf8'}))"
    );
    REQUIRE(yyjson_arr_size(values) == 4);
    for (int i = 0; i < 4; i++) {
        char path[PATH_MAX];
        int length = snprintf(path, sizeof path, "%s/projects/studio/%s", source, names[i]);
        REQUIRE(length > 0 && length < (int)sizeof path);
        int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        REQUIRE(fd >= 0);
        struct stat info;
        bool valid = !fstat(fd, &info) && S_ISREG(info.st_mode) &&
                     info.st_size > 0 && info.st_size < SOURCE_LIMIT;
        char bytes[SOURCE_LIMIT];
        size_t used = 0;
        while (valid && used < (size_t)info.st_size) {
            ssize_t count = read(fd, bytes + used, (size_t)info.st_size - used);
            if (count <= 0) {
                valid = false;
                break;
            }
            used += (size_t)count;
        }
        close(fd);
        REQUIRE(valid);
        bytes[used] = 0;
        const char *loaded = yyjson_get_str(yyjson_arr_get(values, (size_t)i));
        REQUIRE(loaded && strlen(loaded) == used && !memcmp(loaded, bytes, used));
        if (i == 0) {
            REQUIRE(strstr(bytes, "start_scene \"scenes/Studio.kdl\""));
        }
        if (i == 1) {
            REQUIRE(strstr(bytes, "name \"Settings\"\n        prototype \"Settings\""));
            int actors = 0;
            for (const char *at = bytes; (at = strstr(at, "\n    -")); at += 6) {
                actors++;
            }
            REQUIRE(actors == 43);
            REQUIRE(!strstr(bytes, "__outliner_missing__"));
        }
    }
    return true;
}

static bool stop()
{
    if (browser.owned) {
        kill(-browser.process.pid, SIGKILL);
        if (!browser.stop_deadline) {
            browser.stop_deadline = chibi_process_now() + 1;
        }
        while (!chibi_process_poll(&browser.process) &&
               chibi_process_now() < browser.stop_deadline) {
            struct timespec pause = { .tv_nsec = 1000000 };
            nanosleep(&pause, NULL);
        }
        if (!browser.process.finished) {
            return fail("browser process was not reaped after group termination");
        }
        tool_release_process(browser.process.pid);
        browser.owned = false;
    }
    if (browser.diagnostic >= 0) {
        char text[4096];
        for (int i = 0; i < 16; i++) {
            ssize_t count = read(browser.diagnostic, text, sizeof text);
            if (count <= 0) {
                break;
            }
            if (!append_log(text, (size_t)count)) {
                break;
            }
        }
    }
    return !browser.failed;
}

static void cleanup()
{
    stop();
    close_fd(&browser.server);
    close_fd(&browser.input);
    close_fd(&browser.output);
    close_fd(&browser.diagnostic);
    if (browser.artifacts[0]) {
        if (!artifact("browser.log", browser.log, browser.log_size)) {
            fprintf(stderr, "Studio browser: could not save browser.log\n");
        }
    }
    if (browser.log_size) {
        fputs(browser.log, stderr);
    }
    if (browser.temporary_owned) {
        tool_remove_tree(browser.temporary);
    }
    signal(SIGPIPE, browser.previous_pipe);
    yyjson_doc_free(browser.reply);
    free(browser.wire);
    free(browser.png);
    memset(&browser, 0, sizeof browser);
}

TEST_CASE(browser_renders_authored_outliner)
{
    browser.server = browser.input = browser.output = browser.diagnostic = -1;
    browser.previous_pipe = signal(SIGPIPE, SIG_IGN);
    browser.deadline = chibi_process_now() + BROWSER_SECONDS;
    const char *root = getenv("CHIBI_STUDIO_WEB_ROOT");
    const char *artifacts = getenv("CHIBI_STUDIO_BROWSER_ARTIFACTS");
    REQUIRE(root && root[0] == '/' && realpath(root, browser.root));
    REQUIRE(artifacts && artifacts[0] == '/' && realpath(artifacts, browser.artifacts));
    struct stat info;
    REQUIRE(!stat(browser.artifacts, &info) && S_ISDIR(info.st_mode));
    snprintf(browser.temporary, sizeof browser.temporary, "/tmp/chibi-studio-browser-XXXXXX");
    REQUIRE(mkdtemp(browser.temporary));
    browser.temporary_owned = true;
    browser.wire = malloc(MESSAGE_BYTES);
    REQUIRE(browser.wire);
    browser.server = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(browser.server >= 0 && !fcntl(browser.server, F_SETFD, FD_CLOEXEC));
    REQUIRE(!fcntl(browser.server, F_SETFL, O_NONBLOCK));
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    socklen_t size = sizeof address;
    REQUIRE(!bind(browser.server, (struct sockaddr *)&address, sizeof address));
    REQUIRE(!listen(browser.server, 8));
    REQUIRE(!getsockname(browser.server, (struct sockaddr *)&address, &size));
    REQUIRE(start());
    yyjson_val *result = call("Target.createTarget", "{\"url\":\"about:blank\"}");
    const char *target = yyjson_get_str(yyjson_obj_get(result, "targetId"));
    REQUIRE(target && strlen(target) < 128);
    char attach[256];
    snprintf(attach, sizeof attach, "{\"targetId\":\"%s\",\"flatten\":true}", target);
    result = call("Target.attachToTarget", attach);
    const char *session = yyjson_get_str(yyjson_obj_get(result, "sessionId"));
    REQUIRE(session && strlen(session) < sizeof browser.session);
    strcpy(browser.session, session);
    REQUIRE(call("Page.enable", "{}"));
    REQUIRE(call("Runtime.enable", "{}"));
    REQUIRE(
        call("Emulation.setDeviceMetricsOverride", "{\"width\":1280,\"height\":720,\"deviceScaleFactor\":1,\"mobile\":false}")
    );
    REQUIRE(text_command("Page.addScriptToEvaluateOnNewDocument", "source", gate));
    char url[256];
    snprintf(
        url,
        sizeof url,
        "http://127.0.0.1:%u/chibi-studio.html?serial&project-root=projects/"
        "studio&frames=16&fixed-dt=0.016666666666666666",
        (unsigned)ntohs(address.sin_port)
    );
    browser.loaded = false;
    result = text_command("Page.navigate", "url", url);
    REQUIRE(result && !yyjson_obj_get(result, "errorText"));
    REQUIRE(loaded());
    REQUIRE(advance(4, true, false));
    REQUIRE(sources());
    REQUIRE(screenshot("outliner-initial.png"));
    /* Fresh default layout: topbar 40dp, header 32dp, right side 372dp.
     * (1040,56) is inside the authored Filter input at 1280x720/DPR1. */
    REQUIRE(
        call("Input.dispatchMouseEvent", "{\"type\":\"mousePressed\",\"x\":1040,\"y\":56,\"button\":\"left\",\"buttons\":1,\"clickCount\":1}")
    );
    REQUIRE(advance(5, false, false));
    REQUIRE(
        call("Input.dispatchMouseEvent", "{\"type\":\"mouseReleased\",\"x\":1040,\"y\":56,\"button\":\"left\",\"buttons\":0,\"clickCount\":1}")
    );
    REQUIRE(advance(6, false, false));
    REQUIRE(yyjson_get_bool(evaluate("Module._studio_web_wants_text()===1")));
    REQUIRE(type_text("Settings"));
    REQUIRE(advance(8, true, true));
    REQUIRE(screenshot("outliner-settings.png"));
    REQUIRE(
        call("Input.dispatchKeyEvent", "{\"type\":\"keyDown\",\"key\":\"Control\",\"code\":\"ControlLeft\",\"windowsVirtualKeyCode\":17,\"modifiers\":2,\"location\":1}")
    );
    REQUIRE(
        call("Input.dispatchKeyEvent", "{\"type\":\"keyDown\",\"key\":\"a\",\"code\":\"KeyA\",\"windowsVirtualKeyCode\":65,\"modifiers\":2}")
    );
    REQUIRE(advance(9, false, false));
    REQUIRE(
        call("Input.dispatchKeyEvent", "{\"type\":\"keyUp\",\"key\":\"a\",\"code\":\"KeyA\",\"windowsVirtualKeyCode\":65,\"modifiers\":2}")
    );
    REQUIRE(
        call("Input.dispatchKeyEvent", "{\"type\":\"keyUp\",\"key\":\"Control\",\"code\":\"ControlLeft\",\"windowsVirtualKeyCode\":17,\"modifiers\":0,\"location\":1}")
    );
    REQUIRE(type_text("__outliner_missing__"));
    REQUIRE(advance(12, true, true));
    REQUIRE(screenshot("outliner-unmatched.png"));
    REQUIRE(advance(FRAME_LIMIT, false, false));
    REQUIRE(!browser.failed && !browser.runtime_error);
    REQUIRE(stop());
    static const char evidence
        [] = "C transport assertions completed: 16 application frames; three "
             "1280x720 PNGs; "
             "bytes of project.kdl, Studio.kdl, OutlinerRowPolicies.chibi and "
             "OutlinerRows.chibi match the supplied Studio checkout; "
             "nonuniform "
             "changing canvas samples; "
             "no observed JavaScript/Studio status errors.\n"
             "MANUAL REVIEW REQUIRED: initial Studio root count43 with "
             "Settings/Data/Theme rows; "
             "Settings filter count1 with Settings row; unmatched filter "
             "count0 and "
             "No objects match “__outliner_missing__”.\n"
             "Canvas differences do not assert those labels or counts. No "
             "worker-mode, WebGPU, "
             "performance, stale-event or whole-Studio coverage is claimed.\n";
    REQUIRE(artifact("review.txt", evidence, sizeof evidence - 1));
    printf(
        "\nStudio browser artifacts: %s (manual Outliner review required)\n",
        browser.artifacts
    );
    return true;
}

void test_tool_studio_outliner_browser()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(browser_renders_authored_outliner);
    pu_clear_setup();
}
