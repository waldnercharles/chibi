#include "assets/project.h"
#include "authoring/project_migration.h"
#include "authoring/workspace_scaffold.h"
#include "host/authoring_platform_internal.h"
#include "host/workspace_files_web.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "support/rooted_files.h"
#include "support/xalloc.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <unistd.h>
#endif

#ifdef __EMSCRIPTEN__
// clang-format off
EM_JS(int, browser_focused, (), {
    return typeof document !== 'undefined' && !document.hidden && document.hasFocus() ? 1 : 0;
});
// clang-format on
#endif

static const char *state_name(authoring_platform_state state)
{
    static const char *const names[] = { "pending",   "running", "succeeded",
                                         "cancelled", "failed",  "unavailable" };
    assert((unsigned)state < sizeof names / sizeof *names);
    return names[state];
}
void authoring_platform_complete(
    platform_request *r,
    authoring_platform_state state,
    const char *status,
    const char *message,
    const char *value
)
{
    r->state = state;
    snprintf(r->status, sizeof r->status, "%s", status ? status : "");
    snprintf(r->message, sizeof r->message, "%s", message ? message : "");
    if (value) {
        snprintf(r->value, sizeof r->value, "%s", value);
        snprintf(r->input, sizeof r->input, "%s", value);
    }
    r->progress = state == AUTHORING_PLATFORM_SUCCEEDED ? 1 : r->progress;
}
static authoring_platform_result result(const platform_request *r)
{
    if (!r) {
        return (authoring_platform_result){
            .state = "failed",
            .status = "invalid-request",
            .message = "Request handle is absent",
            .value = "",
            .log = "",
            .exit_code = -1
        };
    }
    return (
        authoring_platform_result
    ){ .ok = r->state != AUTHORING_PLATFORM_FAILED &&
             r->state != AUTHORING_PLATFORM_UNAVAILABLE &&
             r->state != AUTHORING_PLATFORM_CANCELLED,
       .request = r->handle,
       .state = state_name(r->state),
       .status = r->status,
       .message = r->message,
       .value = (!strcmp(r->operation, "compile") || !strcmp(r->operation, "export") ||
                 !strcmp(r->operation, "migrate-project"))
                  ? r->value
              : r->text ? r->text
                        : r->value,
       .log = (!strcmp(r->operation, "compile") || !strcmp(r->operation, "export") ||
               !strcmp(r->operation, "migrate-project")) &&
                      r->text
                ? r->text
                : "",
       .progress = r->progress,
       .exit_code = r->exit_code };
}
static platform_request *find(authoring_platform *p, int handle)
{
    for (int i = 0; i < AUTHORING_PLATFORM_REQUESTS_MAX; i++) {
        if (p->requests[i].handle == handle && handle > 0) {
            return &p->requests[i];
        }
    }
    return NULL;
}
void authoring_platform_mailbox_release(platform_mailbox *box)
{
    if (!box) {
        return;
    }
    SDL_LockSpinlock(&box->lock);
    bool last = --box->references == 0;
    SDL_UnlockSpinlock(&box->lock);
    if (last) {
        free(box);
    }
}
#ifndef __EMSCRIPTEN__
static void SDLCALL picked(void *user, const char *const *files, int)
{
    platform_mailbox *box = user;
    SDL_LockSpinlock(&box->lock);
    box->state = !files    ? AUTHORING_PLATFORM_FAILED
               : !files[0] ? AUTHORING_PLATFORM_CANCELLED
                           : AUTHORING_PLATFORM_SUCCEEDED;
    const char *value = files && files[0] ? files[0] : "";
    if (strlen(value) >= sizeof box->value) {
        box->state = AUTHORING_PLATFORM_FAILED;
        snprintf(box->message, sizeof box->message, "Selected path exceeds limit");
    } else {
        snprintf(box->value, sizeof box->value, "%s", value);
        snprintf(box->message, sizeof box->message, "%s", !files ? SDL_GetError() : "");
    }
    box->ready = true;
    SDL_UnlockSpinlock(&box->lock);
    authoring_platform_mailbox_release(box);
}
static void reveal(authoring_platform *p, platform_request *r, const char *relative)
{
    char path[4096], url[sizeof path * 3 + 8];
    size_t at = strlen(p->root);
    if (!relative[0] || relative[0] == '/' || strchr(relative, '\\') ||
        at + strlen(relative) + 2 > sizeof path) {
        authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "unsafe-path", "Expected a workspace-relative path", NULL);
        return;
    }
    memcpy(path, p->root, at + 1);
    const char *part = relative;
    while (*part) {
        const char *end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if (!length || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.')) {
            authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "unsafe-path", "Path has an invalid component", NULL);
            return;
        }
        path[at++] = '/';
        memcpy(path + at, part, length);
        at += length;
        path[at] = 0;
        if (!end) {
            break;
        }
        part = end + 1;
        if (!*part) {
            authoring_platform_complete(
                r,
                AUTHORING_PLATFORM_FAILED,
                "unsafe-path",
                "Path has an empty final component",
                NULL
            );
            return;
        }
    }
    rooted_files *files = rooted_files_open(p->root);
    rooted_file_info info;
    bool valid = files && rooted_files_stat(files, relative, &info) &&
                 (info.regular || info.directory);
    if (files) {
        rooted_files_close(files);
    }
    if (!valid) {
        authoring_platform_complete(
            r,
            AUTHORING_PLATFORM_FAILED,
            "unsafe-path",
            "Path is missing, symbolic, or not regular",
            NULL
        );
        return;
    }
    static const char hex[] = "0123456789ABCDEF";
    const char *prefix = path[0] == '/' && path[1] == '/' ? "file:"
                       : path[0] && path[1] == ':'        ? "file:///"
                                                          : "file://";
    size_t out = strlen(prefix);
    memcpy(url, prefix, out);
    for (size_t i = 0; i < at; i++) {
        unsigned char c = (unsigned char)path[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '/' || c == ':' || c == '-' || c == '_' || c == '.') {
            url[out++] = (char)c;
        } else {
            url[out++] = '%';
            url[out++] = hex[c >> 4];
            url[out++] = hex[c & 15];
        }
    }
    url[out] = 0;
    bool ok = SDL_OpenURL(url);
    authoring_platform_complete(
        r,
        ok ? AUTHORING_PLATFORM_SUCCEEDED : AUTHORING_PLATFORM_FAILED,
        ok ? "submitted" : "url-error",
        ok ? "Path submitted to operating system" : SDL_GetError(),
        NULL
    );
}
static void chooser(authoring_platform *p, platform_request *r, bool folder)
{
    platform_mailbox *box = xcalloc(1, sizeof *box);
    box->references = 2;
    r->mailbox = box;
    if (folder) {
        SDL_ShowOpenFolderDialog(picked, box, p->window, r->value[0] ? r->value : NULL, false);
    } else {
        SDL_ShowOpenFileDialog(picked, box, p->window, NULL, 0, r->value[0] ? r->value : NULL, false);
    }
}
#else
/* Browser promises retain integer tokens, never pointers into a runtime world. */
static authoring_platform *web_services[16];
static int web_token;
EMSCRIPTEN_KEEPALIVE void chibi_authoring_platform_complete(
    int token,
    int state,
    const char *value,
    const char *message
)
{
    for (int i = 0; i < 16; i++) {
        authoring_platform *p = web_services[i];
        if (!p) {
            continue;
        }
        for (int j = 0; j < AUTHORING_PLATFORM_REQUESTS_MAX; j++) {
            platform_request *r = &p->requests[j];
            if (r->token == token && r->state == AUTHORING_PLATFORM_RUNNING) {
                if ((value && strlen(value) > AUTHORING_PLATFORM_TEXT_MAX) ||
                    (unsigned)state > AUTHORING_PLATFORM_UNAVAILABLE) {
                    authoring_platform_complete(
                        r,
                        AUTHORING_PLATFORM_FAILED,
                        "limit",
                        "Browser result exceeds limit",
                        NULL
                    );
                    return;
                }
                free(r->text);
                r->text = xstrdup(value ? value : "");
                authoring_platform_complete(
                    r,
                    (authoring_platform_state)state,
                    state == AUTHORING_PLATFORM_SUCCEEDED ? "ok" : "browser-error",
                    message,
                    NULL
                );
                return;
            }
        }
    }
}
EMSCRIPTEN_KEEPALIVE bool chibi_authoring_platform_migration_complete(
    int token,
    int state,
    const char *root,
    const char *message,
    const char *log
)
{
    if ((state != AUTHORING_PLATFORM_SUCCEEDED && state != AUTHORING_PLATFORM_FAILED) ||
        !root || strlen(root) >= 4096 || !message || strlen(message) > 16384 ||
        !log || strlen(log) > AUTHORING_PLATFORM_LOG_MAX) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        authoring_platform *p = web_services[i];
        if (!p) {
            continue;
        }
        for (int j = 0; j < AUTHORING_PLATFORM_REQUESTS_MAX; j++) {
            platform_request *r = &p->requests[j];
            if (r->token != token || r->state != AUTHORING_PLATFORM_RUNNING ||
                strcmp(r->operation, "migrate-project")) {
                continue;
            }
            int version = 0;
            char error[256] = { 0 };
            if (state == AUTHORING_PLATFORM_SUCCEEDED &&
                (!project_migration_version(root, &version, error, sizeof error) ||
                 version != CHIBI_PROJECT_VERSION)) {
                free(r->text);
                r->text = xstrdup(log);
                authoring_platform_complete(
                    r,
                    AUTHORING_PLATFORM_FAILED,
                    "invalid-migration-result",
                    *error ? error : "Migrated project is not current",
                    NULL
                );
                return false;
            }
            free(r->text);
            r->text = xstrdup(log);
            authoring_platform_complete(
                r,
                (authoring_platform_state)state,
                state == AUTHORING_PLATFORM_SUCCEEDED ? "migrated" : "migration-failed",
                message,
                root
            );
            return true;
        }
    }
    return false;
}
// clang-format off
EM_JS(void, authoring_platform_web_request, (int token, const char *operation, const char *value, const char *rootPath, const char *sourcePath), {
    const op = UTF8ToString(operation), text = UTF8ToString(value), workspaceRoot = UTF8ToString(rootPath), sourcePathValue = UTF8ToString(sourcePath);
    const jobs = Module.chibiPlatformRequests ||= new Map();
    const job = {cancelled: false}; jobs.set(token, job);
    const discard = () => {
        if (!job.staging) return;
        const remove = path => { const st=FS.lstat(path); if(FS.isDir(st.mode)){for(const name of FS.readdir(path)){if(name!=='.'&&name!=='..')remove(path+'/'+name);}FS.rmdir(path);}else FS.unlink(path); };
        try { remove(job.staging); } catch {}
        job.staging = null;
    };
    job.discard = discard;
    const finish = (state, value="", message="") => {
        if (job.cancelled) return;
        jobs.delete(token);
        if (state !== 2) discard();
        const a = stringToNewUTF8(value), b = stringToNewUTF8(message);
        Module._chibi_authoring_platform_complete(token, state, a, b);
        _free(a); _free(b);
    };
    const {pathSafe,zip,unzip,captureBytes,installBytes} = Module.browserWorkspaceFiles;
    (async () => {
        try {
            if (op === 'discard-project-copy') { Module.browserWorkspaceFiles.remove(text);finish(2,"",'Unopened migration copy discarded');return; }
            if (op === 'migrate-project') {
                const options=JSON.parse(text);
                if(!Module.browserProjectMigration || !Module.browserProjectMigration.start(token,options.Root,(ok,root,receipt,log)=>{
                    if(job.cancelled || jobs.get(token)!==job)return false;
                    const suffix='\n'+(ok?receipt:""),raw=new TextEncoder().encode(log||""),limit=32768-new TextEncoder().encode(suffix).length-32;
                    let length=Math.min(raw.length,limit);while(length>0&&length<raw.length&&(raw[length]&0xc0)===0x80)--length;
                    const retained=new TextDecoder().decode(raw.subarray(0,length))+(length<raw.length?'\n[diagnostics truncated]':"")+suffix;
                    const a=stringToNewUTF8(ok?root:""),b=stringToNewUTF8(ok?'Migration copy ready; original unchanged.':receipt),c=stringToNewUTF8(retained);
                    let accepted=false;
                    try{accepted=!!Module._chibi_authoring_platform_migration_complete(token,ok?2:4,a,b,c);}finally{_free(a);_free(b);_free(c);}
                    jobs.delete(token);if(accepted)Module.browserProjectMigration.detach(token);
                    return accepted;
                }))throw Error('Project migration worker unavailable');
                job.discard=()=>Module.browserProjectMigration?.cancel(token);
                return;
            }
            if (op === 'clipboard-read') { const data = await navigator.clipboard.readText(); if (new TextEncoder().encode(data).length > 16384) throw Error('Clipboard exceeds 16 KiB'); finish(2, data); return; }
            if (op === 'clipboard-write') { await navigator.clipboard.writeText(text); finish(2); return; }
            if (op === 'open-url') { const tab = window.open(text, '_blank'); if (tab) tab.opener = null; if (!tab) throw Error('Browser blocked or could not confirm opening the URL'); finish(2, "", 'URL submitted to browser'); return; }
            if (op === 'export-project') {
                const files=captureBytes(workspaceRoot,[]);
                const link=document.createElement('a');link.href=URL.createObjectURL(zip(files));link.download='project.zip';link.click();setTimeout(()=>URL.revokeObjectURL(link.href),40000);
                finish(2,"",'ZIP download submitted to browser');return;
            }
            if(op==='import-file'){
                if(!sourcePathValue||!pathSafe(text))throw Error('Import requires selected source and workspace-relative destination');
                const stat=FS.lstat(sourcePathValue);if(!FS.isFile(stat.mode)||stat.size>16777216)throw Error('Selected source is not a bounded regular file');
                const target=workspaceRoot+'/'+text,temporary=target+'.platform-'+token;
                const parent=text.split('/').slice(0,-1);let prefix=workspaceRoot;
                for(const segment of parent){prefix+='/'+segment;if(!FS.isDir(FS.lstat(prefix).mode))throw Error('Destination parent must be an existing real directory');}
                if(FS.analyzePath(target,true).exists)throw Error('Import destination already exists');
                try {FS.writeFile(temporary,FS.readFile(sourcePathValue),{flags:'wx'});if(FS.analyzePath(target,true).exists)throw Error('Import destination already exists');FS.rename(temporary,target);}catch(error){try{FS.unlink(temporary);}catch{}throw error;}
                finish(2,text,'Selected bytes copied exclusively; no decoding or conversion performed');return;
            }
            if (op !== 'choose-file' && op !== 'choose-folder' && op !== 'import-project') { finish(5, "", 'Operation has no browser backend'); return; }
            const input = document.createElement('input'); input.type = 'file';
            if(op==='import-project')input.accept='.zip,application/zip';
            if (op === 'choose-folder') { input.webkitdirectory = true; input.multiple = true; }
            input.addEventListener('cancel', () => finish(3), {once:true});
            input.onchange = async () => {
                try {
                    const files = [...input.files].sort((a,b) => ((a.webkitRelativePath || a.name) < (b.webkitRelativePath || b.name) ? -1 : (a.webkitRelativePath || a.name) > (b.webkitRelativePath || b.name) ? 1 : 0));
                    if (!files.length) { finish(3); return; }
                    if (files.length > 4096 || files.some(f => f.size > (op==='import-project'?100663296:16777216)) || files.reduce((n,f) => n+f.size,0) > (op==='import-project'?100663296:67108864)) throw Error('Selection exceeds file/count/byte budget');
                    const root = '/tmp/chibi-platform-' + token; job.staging = root;
                    if(op==='import-project'){
                        const archive=await unzip(await files[0].arrayBuffer(),()=>job.cancelled);if(job.cancelled)return;
                        const manifest=archive.map(([name])=>name).filter(n=>n==='project.kdl'||n.endsWith('/project.kdl')).sort((a,b)=>a.split('/').length-b.split('/').length||(a<b?-1:a>b?1:0))[0];
                        if(!manifest)throw Error('ZIP has no project.kdl');
                        const installed=installBytes(archive,()=>job.cancelled);job.staging=installed;
                        finish(2,installed+'/'+manifest.slice(0,-'project.kdl'.length));return;
                    }
                    FS.mkdirTree(root);
                    const paths = [];
                    for (const file of files) {
                        const relative = file.webkitRelativePath || file.name;
                        if (!pathSafe(relative)) throw Error('Invalid selected path');
                        const path = root + '/' + relative;
                        FS.mkdirTree(path.slice(0,path.lastIndexOf('/')));
                        const data = new Uint8Array(await file.arrayBuffer());
                        if (job.cancelled) { discard(); return; }
                        FS.writeFile(path, data, {flags:'wx'}); paths.push(path);
                        if (job.cancelled) { discard(); return; }
                    }
                    finish(2, op === 'choose-file' ? paths[0] : root + '/' + files[0].webkitRelativePath.split('/')[0]);
                } catch (error) { finish(4,"",error.message || String(error)); }
            };
            input.click();
        } catch (error) { finish(4, "", error.message || String(error)); }
    })();
});
EM_JS(void, web_cancel, (int token), {
    const jobs = Module.chibiPlatformRequests; const job = jobs?.get(token);
    if (job) { job.cancelled = true; job.discard(); jobs.delete(token); }
});
// clang-format on
#endif
static bool terminal(const platform_request *r)
{
    return r->state >= AUTHORING_PLATFORM_SUCCEEDED;
}
authoring_platform *authoring_platform_new(const authoring_platform_config *config, char *error, size_t size)
{
    assert(config && (!size || error));
    workspace_files_web_init();
    if (size) {
        error[0] = 0;
    }
    rooted_files *files = config->root ? rooted_files_open(config->root) : NULL;
    char *root = files ? rooted_files_host_path(files) : NULL;
    if (files) {
        rooted_files_close(files);
    }
    if (config->root && !root) {
        if (size) {
            snprintf(error, size, "Platform host requires an existing workspace root");
        }
        return NULL;
    }
    authoring_platform *p = xcalloc(1, sizeof *p);
    p->root = root;
    p->compiler = xstrdup(config->compiler ? config->compiler : "");
    p->launcher = xstrdup(config->launcher ? config->launcher : "");
    p->adapter = config->adapter;
    p->request = config->request;
    p->cancel = config->cancel;
    p->window = config->window;
    p->next_handle = 1;
    p->scale = 1;
#ifdef __EMSCRIPTEN__
    bool registered = false;
    for (int i = 0; i < 16; i++) {
        if (!web_services[i]) {
            web_services[i] = p;
            registered = true;
            break;
        }
    }
    if (!registered) {
        authoring_platform_free(p);
        if (size) {
            snprintf(error, size, "Browser platform service limit exceeded");
        }
        return NULL;
    }
#endif
    return p;
}
void authoring_platform_free(authoring_platform *p)
{
    if (!p) {
        return;
    }
    for (int i = 0; i < AUTHORING_PLATFORM_REQUESTS_MAX; i++) {
        platform_request *r = &p->requests[i];
#ifdef __EMSCRIPTEN__
        if (r->token) {
            web_cancel(r->token);
        }
#endif
        if (r->external && !terminal(r) && p->cancel) {
            p->cancel(p->adapter, r->handle);
        }
        authoring_platform_free_job(r);
        authoring_platform_mailbox_release(r->mailbox);
        free(r->text);
    }
#ifdef __EMSCRIPTEN__
    for (int i = 0; i < 16; i++) {
        if (web_services[i] == p) {
            web_services[i] = NULL;
        }
    }
#endif
    free(p->project);
    free(p->root);
    free(p->compiler);
    free(p->launcher);
    free(p);
}
authoring_platform_result authoring_platform_request(
    authoring_platform *p,
    const char *op,
    const char *value,
    double number,
    double other,
    int source
)
{
    assert(p);
    if (!op || !value || strnlen(op, 32) >= 32 ||
        strnlen(value, AUTHORING_PLATFORM_TEXT_MAX + 1) > AUTHORING_PLATFORM_TEXT_MAX ||
        !isfinite(number) || !isfinite(other)) {
        return result(NULL);
    }
    platform_request *r = NULL;
    for (int i = 0; i < AUTHORING_PLATFORM_REQUESTS_MAX; i++) {
        if (!p->requests[i].handle) {
            r = &p->requests[i];
            break;
        }
    }
    if (!r || p->next_handle == INT_MAX) {
        authoring_platform_result out = result(NULL);
        out.status = "limit";
        out.message = "Release finished requests before starting another";
        return out;
    }
    *r = (platform_request){ .handle = p->next_handle++,
                             .state = AUTHORING_PLATFORM_RUNNING,
                             .started = SDL_GetTicks(),
                             .exit_code = -1,
                             .source = source };
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
    r->output = r->import_source = r->import_output = r->import_parent = -1;
#endif
    snprintf(r->operation, sizeof r->operation, "%s", op);
    static const char *const allowed[] = { "choose-file",
                                           "choose-folder",
                                           "clipboard-read",
                                           "clipboard-write",
                                           "open-url",
                                           "reveal-path",
                                           "quit",
                                           "window-size",
                                           "ui-scale",
                                           "text-input",
                                           "compile",
                                           "export",
                                           "import-file",
                                           "import-project",
                                           "export-project",
                                           "activate-project",
                                           "create-workspace",
                                           "inspect-project",
                                           "migrate-project",
                                           "discard-project-copy" };
    bool known = false;
    for (size_t i = 0; i < sizeof allowed / sizeof *allowed; i++) {
        known |= !strcmp(op, allowed[i]);
    }
    if (!known) {
        authoring_platform_complete(
            r,
            AUTHORING_PLATFORM_FAILED,
            "invalid-operation",
            "Platform operation is not supported",
            NULL
        );
        return result(r);
    }
    if (!p->root && (!strcmp(op, "reveal-path") || !strcmp(op, "compile") ||
                     !strcmp(op, "export") || !strcmp(op, "import-file") ||
                     !strcmp(op, "export-project"))) {
        authoring_platform_complete(
            r,
            AUTHORING_PLATFORM_UNAVAILABLE,
            "no-workspace",
            "Open a workspace before requesting project file operations",
            NULL
        );
        return result(r);
    }
    if (!strcmp(op, "text-input")) {
        if (number != 0 && number != 1) {
            authoring_platform_complete(
                r,
                AUTHORING_PLATFORM_FAILED,
                "invalid",
                "Text input focus must be zero or one",
                NULL
            );
        } else {
            p->text_input = number == 1;
            authoring_platform_complete(
                r,
                AUTHORING_PLATFORM_SUCCEEDED,
                "requested",
                "Host must apply requested text input focus",
                NULL
            );
        }
        return result(r);
    }
    if (p->request &&
        p->request(p->adapter, p, r->handle, op, value, number, other, source)) {
        r->external = true;
        return result(r);
    }

    if (!strcmp(op, "create-workspace")) {
        char *root = NULL;
        char error[512] = { 0 };
        bool ok = false;
#ifdef __EMSCRIPTEN__
        char parent[] = "/tmp/chibi-created-XXXXXX";
        if (mkdtemp(parent)) {
            ok = workspace_scaffold_create(parent, value, &root, error, sizeof error);
            if (!ok) {
                rmdir(parent);
            }
        } else {
            snprintf(error, sizeof error, "Could not create a fresh browser workspace folder");
        }
#else
        platform_request *selected = find(p, source);
        if (selected && selected->state == AUTHORING_PLATFORM_SUCCEEDED &&
            !strcmp(selected->operation, "choose-folder")) {
            ok = workspace_scaffold_create(
                selected->text ? selected->text : selected->value,
                value,
                &root,
                error,
                sizeof error
            );
        } else {
            snprintf(error, sizeof error, "Choose a parent folder before creating a workspace");
        }
#endif
        authoring_platform_complete(
            r,
            ok ? AUTHORING_PLATFORM_SUCCEEDED : AUTHORING_PLATFORM_FAILED,
            ok ? "created" : "creation-failed",
            ok ? "Project created" : error,
            root
        );
        free(root);
        return result(r);
    }
    if (!strcmp(op, "inspect-project")) {
        int version = 0;
        char error[512] = { 0 }, message[256];
        rooted_files *files = rooted_files_open(value);
        char *root = files ? rooted_files_host_path(files) : NULL;
        if (files) {
            rooted_files_close(files);
        }
        bool ok = root &&
                  project_migration_version(root, &version, error, sizeof error) &&
                  version >= CHIBI_PROJECT_OLDEST_VERSION &&
                  version <= CHIBI_PROJECT_VERSION;
        snprintf(
            message,
            sizeof message,
            "Project version %d; this engine opens version %d. Upgrade a copy "
            "before opening older projects.",
            version,
            CHIBI_PROJECT_VERSION
        );
        authoring_platform_complete(
            r,
            ok ? AUTHORING_PLATFORM_SUCCEEDED : AUTHORING_PLATFORM_FAILED,
            ok ? (version == CHIBI_PROJECT_VERSION ? "current" : "upgrade-required")
               : "invalid-project",
            ok ? message
               : (*error ? error : "Project version is unsupported by this engine"),
            root
        );
        free(root);
        return result(r);
    }
    if (!strcmp(op, "migrate-project")) {
        yyjson_doc *options = yyjson_read(value, strlen(value), 0);
        yyjson_val *object = options ? yyjson_doc_get_root(options) : NULL;
        yyjson_val *root_value = yyjson_obj_get(object, "Root"),
                   *name_value = yyjson_obj_get(object, "Name");
        const char *root = yyjson_get_str(root_value),
                   *name = yyjson_get_str(name_value);
        bool valid = yyjson_is_obj(object) && yyjson_obj_size(object) == 2 &&
                     root && name && strlen(root) == yyjson_get_len(root_value) &&
                     strlen(name) == yyjson_get_len(name_value) &&
                     strlen(root) < sizeof r->input && strlen(name) <= 128 &&
                     rooted_files_path_valid(name) && !strchr(name, '/');
        if (valid) {
            snprintf(r->input, sizeof r->input, "%s", root);
        }
#ifndef __EMSCRIPTEN__
        platform_request *selected = find(p, source);
        rooted_files *parent = selected && selected->state == AUTHORING_PLATFORM_SUCCEEDED &&
                                       !strcmp(selected->operation, "choose-folder")
                                 ? rooted_files_open(selected->value)
                                 : NULL;
        char *parent_path = parent ? rooted_files_host_path(parent) : NULL;
        int length = valid && parent_path
                       ? snprintf(r->value, sizeof r->value, "%s/%s", parent_path, name)
                       : -1;
        valid = valid && length > 0 && (size_t)length < sizeof r->value;
        if (parent) {
            rooted_files_close(parent);
        }
        free(parent_path);
#endif
        yyjson_doc_free(options);
        if (!valid) {
            authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "invalid-migration", "Select a supported project, a parent folder and one new folder name", NULL);
            return result(r);
        }
#ifndef __EMSCRIPTEN__
        authoring_platform_start_job(p, r);
        return result(r);
#endif
    }
    if (strlen(value) < sizeof r->value) {
        snprintf(r->value, sizeof r->value, "%s", value);
        snprintf(r->input, sizeof r->input, "%s", value);
    } else if (strcmp(op, "clipboard-write")) {
        authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "limit", "Path or URL exceeds limit", NULL);
        return result(r);
    }
    if (!strcmp(op, "activate-project")) {
        if (p->project) {
            authoring_platform_complete(
                r,
                AUTHORING_PLATFORM_FAILED,
                "busy",
                "A project activation is already pending",
                NULL
            );
            return result(r);
        }
        rooted_files *files = rooted_files_open(value);
        char *root = files ? rooted_files_host_path(files) : NULL;
        unsigned char *text = NULL;
        size_t length = 0;
        bool valid = files &&
                     rooted_files_read(files, CHIBI_PROJECT_MANIFEST, 65536, &text, &length) &&
                     length > 0;
        if (files) {
            rooted_files_close(files);
        }
        av_doc *manifest = valid && !memchr(text, 0, length)
                             ? av_doc_read((const char *)text, NULL, 0)
                             : NULL;
        valid = manifest && av_get_int(av_obj_get(av_doc_root(manifest), "version")) ==
                                CHIBI_PROJECT_VERSION;
        av_doc_free(manifest);
        free(text);
        if (!valid || !root || strlen(root) >= sizeof r->value) {
            free(root);
            authoring_platform_complete(
                r,
                AUTHORING_PLATFORM_FAILED,
                "invalid-project",
                "Expected a current-version project with a bounded regular "
                "project.kdl manifest",
                NULL
            );
        } else {
            p->project = root;
            p->project_request = r->handle;
            authoring_platform_complete(
                r,
                AUTHORING_PLATFORM_RUNNING,
                "pending-host",
                "Project activation awaits host preparation",
                root
            );
        }
        return result(r);
    }
    if (!strcmp(op, "quit")) {
        p->quit = true;
        authoring_platform_complete(r, AUTHORING_PLATFORM_SUCCEEDED, "requested", "Host must consume quit request", NULL);
        return result(r);
    }
    if (!strcmp(op, "ui-scale")) {
        if (number < .5 || number > 3) {
            authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "invalid", "UI scale must be between 0.5 and 3", NULL);
        } else {
            p->scale = number;
            authoring_platform_complete(
                r,
                AUTHORING_PLATFORM_SUCCEEDED,
                "requested",
                "Host must apply requested UI scale",
                NULL
            );
        }
        return result(r);
    }
    if (!strcmp(op, "window-size")) {
        if (!p->window || number < 320 || number > 16384 || other < 200 ||
            other > 16384 || floor(number) != number || floor(other) != other) {
            authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "invalid", "Window dimensions must be integer pixels within 320..16384 by 200..16384", NULL);
        } else if (!SDL_SetWindowSize(p->window, (int)number, (int)other)) {
            authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "window-error", SDL_GetError(), NULL);
        } else {
            authoring_platform_complete(r, AUTHORING_PLATFORM_SUCCEEDED, "requested", "Window size request submitted", NULL);
        }
        return result(r);
    }
#ifndef __EMSCRIPTEN__
    if (!strcmp(op, "compile") || !strcmp(op, "export") || !strcmp(op, "import-file")) {
        authoring_platform_start_job(p, r);
        return result(r);
    }
#endif
    if (!strcmp(op, "open-url") &&
        (strncmp(value, "https://", 8) && strncmp(value, "http://", 7))) {
        authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "invalid", "Only HTTP(S) URLs are supported", NULL);
        return result(r);
    }
#ifdef __EMSCRIPTEN__
    if (strcmp(op, "choose-file") && strcmp(op, "choose-folder") &&
        strcmp(op, "clipboard-read") && strcmp(op, "clipboard-write") &&
        strcmp(op, "open-url") && strcmp(op, "import-project") &&
        strcmp(op, "export-project") && strcmp(op, "import-file") &&
        strcmp(op, "migrate-project") && strcmp(op, "discard-project-copy")) {
        authoring_platform_complete(
            r,
            AUTHORING_PLATFORM_UNAVAILABLE,
            "unavailable",
            "Operation has no browser backend",
            NULL
        );
        return result(r);
    }
    if (web_token == INT_MAX) {
        authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "limit", "Browser request identity exhausted", NULL);
        return result(r);
    }
    r->token = ++web_token;
    platform_request *selected = find(p, source);
    const char *source_path = selected && selected->state == AUTHORING_PLATFORM_SUCCEEDED &&
                                      !strcmp(selected->operation, "choose-file")
                                ? selected->text ? selected->text : selected->value
                                : "";
    authoring_platform_web_request(r->token, op, value, p->root ? p->root : "", source_path);
#else
    if (!strcmp(op, "choose-file") || !strcmp(op, "choose-folder") ||
        !strcmp(op, "import-project")) {
        chooser(p, r, strcmp(op, "choose-file") != 0);
    } else if (!strcmp(op, "clipboard-read")) {
        char *text = SDL_GetClipboardText();
        if (!text) {
            authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "clipboard-error", SDL_GetError(), NULL);
        } else if (strnlen(text, AUTHORING_PLATFORM_TEXT_MAX + 1) > AUTHORING_PLATFORM_TEXT_MAX) {
            authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, "limit", "Clipboard exceeds 16 KiB", NULL);
        } else {
            r->text = xstrdup(text);
            authoring_platform_complete(r, AUTHORING_PLATFORM_SUCCEEDED, "ok", "", NULL);
        }
        SDL_free(text);
    } else if (!strcmp(op, "clipboard-write")) {
        bool ok = SDL_SetClipboardText(value);
        authoring_platform_complete(
            r,
            ok ? AUTHORING_PLATFORM_SUCCEEDED : AUTHORING_PLATFORM_FAILED,
            ok ? "ok" : "clipboard-error",
            ok ? "" : SDL_GetError(),
            NULL
        );
    } else if (!strcmp(op, "reveal-path")) {
        reveal(p, r, value);
    } else if (!strcmp(op, "open-url")) {
        bool ok = SDL_OpenURL(value);
        authoring_platform_complete(
            r,
            ok ? AUTHORING_PLATFORM_SUCCEEDED : AUTHORING_PLATFORM_FAILED,
            ok ? "submitted" : "url-error",
            ok ? "URL submitted to operating system" : SDL_GetError(),
            NULL
        );
    } else {
        authoring_platform_complete(
            r,
            AUTHORING_PLATFORM_UNAVAILABLE,
            "unavailable",
            "Operation has no native adapter",
            NULL
        );
    }
#endif
    return result(r);
}
void authoring_platform_tick(authoring_platform *p)
{
    assert(p);
    for (int i = 0; i < AUTHORING_PLATFORM_REQUESTS_MAX; i++) {
        platform_request *r = &p->requests[i];
        authoring_platform_poll_job(r);
        if (!r->handle) {
            continue;
        }
        platform_mailbox *box = r->mailbox;
        if (box) {
            SDL_LockSpinlock(&box->lock);
            if (box->ready && !terminal(r)) {
                authoring_platform_complete(
                    r,
                    box->state,
                    box->state == AUTHORING_PLATFORM_SUCCEEDED ? "ok" : "dialog-error",
                    box->message,
                    box->value
                );
            }
            bool ready = box->ready;
            SDL_UnlockSpinlock(&box->lock);
            if (ready) {
                r->mailbox = NULL;
                authoring_platform_mailbox_release(box);
            }
        }
        if (!terminal(r) && SDL_GetTicks() - r->started > 58000) {
            authoring_platform_cancel(p, r->handle);
            snprintf(r->status, sizeof r->status, "timeout");
            snprintf(r->message, sizeof r->message, "Request exceeded its 58 second work deadline");
        }
    }
}
authoring_platform_result authoring_platform_poll(authoring_platform *p, int handle)
{
    authoring_platform_tick(p);
    return result(find(p, handle));
}
authoring_platform_result authoring_platform_cancel(authoring_platform *p, int handle)
{
    platform_request *r = find(p, handle);
    if (!r || terminal(r)) {
        return result(r);
    }
    if (r->external && p->cancel) {
        p->cancel(p->adapter, r->handle);
    }
    authoring_platform_cancel_job(r);
    if (p->project_request == handle) {
        free(p->project);
        p->project = NULL;
        p->project_request = 0;
    }
#ifdef __EMSCRIPTEN__
    if (r->token) {
        web_cancel(r->token);
    }
#endif
    authoring_platform_complete(
        r,
        AUTHORING_PLATFORM_CANCELLED,
        "cancelled",
        "Request cancelled; late chooser results are ignored",
        NULL
    );
    if (!strcmp(r->operation, "migrate-project")) {
        snprintf(r->message, sizeof r->message, "Migration cancelled; original unchanged. Incomplete destination may remain; inspect Value and Log.");
    }
    return result(r);
}
bool authoring_platform_release(authoring_platform *p, int handle)
{
    platform_request *r = find(p, handle);
    if (!r || !terminal(r)) {
        return false;
    }
    authoring_platform_free_job(r);
    authoring_platform_mailbox_release(r->mailbox);
    free(r->text);
    *r = (platform_request){ 0 };
    return true;
}
bool authoring_platform_resolve(
    authoring_platform *p,
    int handle,
    authoring_platform_state state,
    const char *status,
    const char *message,
    const char *value
)
{
    platform_request *r = find(p, handle);
    if (!r || terminal(r) || state < AUTHORING_PLATFORM_SUCCEEDED ||
        state > AUTHORING_PLATFORM_UNAVAILABLE || !status || !message || !value ||
        strnlen(value, AUTHORING_PLATFORM_TEXT_MAX + 1) > AUTHORING_PLATFORM_TEXT_MAX ||
        strlen(status) >= sizeof r->status || strlen(message) >= sizeof r->message) {
        return false;
    }
    free(r->text);
    r->text = xstrdup(value);
    authoring_platform_complete(r, state, status, message, NULL);
    return true;
}
authoring_platform_environment authoring_platform_environment_get(authoring_platform *p)
{
    int width = 0, height = 0;
    if (p->window) {
        SDL_GetWindowSize(p->window, &width, &height);
    }
    authoring_platform_environment out = {
        .focused = p->window && (SDL_GetWindowFlags(p->window) & SDL_WINDOW_INPUT_FOCUS),
        .clipboard = true,
        .file_picker = true,
        .folder_picker = true,
        .platform = SDL_GetPlatform(),
        .theme = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK  ? "dark"
               : SDL_GetSystemTheme() == SDL_SYSTEM_THEME_LIGHT ? "light"
                                                                : "unknown",
        .width = width,
        .height = height,
        .close_request = p->close_request,
        .scale = p->scale
    };
#ifdef __EMSCRIPTEN__
    out.web = true;
    out.focused = browser_focused();
#else
    out.native = true;
    out.compile = p->compiler[0];
    out.export = p->launcher[0];
#endif
    if (p->frame_focus_known) {
        out.focused = p->frame_focus;
    }
    return out;
}
bool authoring_platform_quit_requested(const authoring_platform *p)
{
    return p->quit;
}
void authoring_platform_close_request(authoring_platform *p)
{
    if (p && p->close_request < INT_MAX) {
        p->close_request++;
    }
}
double authoring_platform_ui_scale(const authoring_platform *p)
{
    return p->scale;
}

const char *authoring_platform_pending_project(const authoring_platform *p)
{
    return p->project;
}
void authoring_platform_project_complete(authoring_platform *p, bool success, const char *error)
{
    platform_request *r = find(p, p->project_request);
    assert(p->project && r && !terminal(r));
    if (success) {
        workspace_files_web_adopt(p->project);
    }
    authoring_platform_complete(
        r,
        success ? AUTHORING_PLATFORM_SUCCEEDED : AUTHORING_PLATFORM_FAILED,
        success ? "ok" : "activation-error",
        success ? "Project activated" : error,
        NULL
    );
    free(p->project);
    p->project = NULL;
    p->project_request = 0;
}

bool authoring_platform_text_input_requested(const authoring_platform *p)
{
    return p && p->text_input;
}
void authoring_platform_set_frame_focus(authoring_platform *p, bool focused)
{
    if (p) {
        p->frame_focus_known = true;
        p->frame_focus = focused;
    }
}
