#include "host/process.h"
#include "support/sha256.h"
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

TEST_CASE(authored_checkpoint_v3_preserves_server_validation)
{
    char test_directory[] = "/tmp/chibi-collab-protocol-XXXXXX";
    REQUIRE(mkdtemp(test_directory));
    char input[256], log[256];
    snprintf(input, sizeof input, "%s/input.json", test_directory);
    snprintf(log, sizeof log, "%s/output.log", test_directory);
    char file_hash[65], state_hash[65], archive_hash[65], snapshot_hash[65];
    unsigned char state[36] = { 'C', 'H', 'B', 'I',     'D',
                                'O', 'C', 'S', [8] = 1, [16] = 1 };
    sha256_hex("x", 1, file_hash);
    sha256_hex(state, sizeof state, state_hash);
    char files[512], attachments[512], canonical[1200], archive[1400],
        unsigned_snapshot[1700], snapshot[1900];
    snprintf(files, sizeof files, "[{\"path\":\"project.kdl\",\"base64\":\"eA==\",\"sha256\":\"%s\"}]", file_hash);
    snprintf(attachments, sizeof attachments, "[{\"name\":\"workspace-documents-v1\",\"base64\":\"Q0hCSURPQ1MBAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAAAA\",\"bytes\":36,\"sha256\":\"%s\"}]", state_hash);
    snprintf(canonical, sizeof canonical, "{\"files\":%s,\"attachments\":%s}", files, attachments);
    sha256_hex(canonical, strlen(canonical), archive_hash);
    snprintf(
        archive,
        sizeof archive,
        "{\"files\":%s,\"bytes\":37,\"sha256\":\"%s\",\"attachments\":%s}",
        files,
        archive_hash,
        attachments
    );
    snprintf(
        unsigned_snapshot,
        sizeof unsigned_snapshot,
        "{\"checkpoint_version\":3,\"workspace\":{\"version\":3,\"active_"
        "path\":\"\",\"presentation\":{}},\"archive\":%s}",
        archive
    );
    sha256_hex(unsigned_snapshot, strlen(unsigned_snapshot), snapshot_hash);
    snprintf(
        snapshot,
        sizeof snapshot,
        "%.*s,\"sha256\":\"%s\"}",
        (int)strlen(unsigned_snapshot) - 1,
        unsigned_snapshot,
        snapshot_hash
    );
    tool_write(input, snapshot);
    const char *const script_parts[] = {
        "import {readFileSync} from 'node:fs';import {runInNewContext} from "
        "'node:vm';import {webcrypto,createHash} from 'node:crypto';import "
        "{pathToFileURL} from 'node:url';const "
        "{validateSnapshot,validateEnvelope,validateClientMessage}=await "
        "import(pathToFileURL(process.cwd()+'/tools/collab/server/"
        "protocol.mjs').href);const "
        "base=JSON.parse(readFileSync(process.argv[2],'utf8'));const "
        "codes=[];const check=change=>{const "
        "value=structuredClone(base);change(value);try{validateSnapshot(value);"
        "codes.push('ok')}catch(error){codes.push(error.code||error.message)}};"
        "check(()=>{});check(v=>v.archive.files.push({...v.archive.files[0]}));"
        "check(v=>v.archive.files[0].path='../"
        "project.kdl');check(v=>v.archive.files[0].base64='eB==');check(v=>v."
        "archive.files[0].sha256='0'.repeat(64));check(v=>v.archive.bytes=2);"
        "check(v=>v.archive.sha256='0'.repeat(64));check(v=>v.sha256='0'."
        "repeat(64));check(v=>v.checkpoint_version=1);check(v=>v.workspace."
        "active_path='../"
        "bad');check(v=>v.archive.attachments=[]);check(v=>v.archive."
        "attachments.push({...v.archive.attachments[0]}));check(v=>v.archive."
        "attachments[0].name='unknown');check(v=>v.archive.attachments[0]."
        "base64='eA==');check(v=>v.archive.attachments[0].bytes=35);check(v=>v."
        "archive.attachments[0].sha256='0'.repeat(64));check(v=>v.archive."
        "attachments[0].base64='R'+v.archive.attachments[0].base64.slice(1));"
        "try{validateEnvelope({protocol:2,room:'r',client:'c',seq:0,revision:0,"
        "kind:'ack',payload:{}});codes.push('ok')}catch(error){codes.push("
        "error.code)};const "
        "atomic={family:'document-edits',anchor:'content/"
        "A.chibi',edits:[{path:'content/A.chibi',text:'source'},{path:'content/"
        "A.chibi.meta',text:'metadata'}]};const action=change=>{const "
        "payload=structuredClone(atomic);change(payload);try{"
        "validateClientMessage(validateEnvelope({protocol:1,room:'r',client:'c'"
        ",seq:1,revision:0,kind:'proposal',payload}));codes.push('ok')}catch("
        "error){codes.push(error.code||error.message)}};action(()=>{});action("
        "v=>v.edits[1].path=v.edits[0].path);action(v=>v.edits[1].path='../"
        "bad');action(v=>v.anchor='content/missing.chibi');",
        "action(v=>v.edits[0].extra=true);action(v=>delete "
        "v.edits[0].path);action(v=>v.edits=Array(9).fill(v.edits[0]));action("
        "v=>v.edits[0].text='bad'+String.fromCharCode(0));action(v=>v.edits[0]."
        "text=String.fromCharCode(0xd800));action(v=>v.edits[0].text='x'."
        "repeat(16384));action(v=>v.anchor=42);const "
        "helperSource=readFileSync('src/host/"
        "workspace_files_web.c','utf8');\nconst marker='EM_JS(void, "
        "workspace_files_web_js_init, (), {';\nconst "
        "begin=helperSource.indexOf(marker)+marker.length,end=helperSource."
        "indexOf('\\n});',begin);\nif(begin<marker.length||end<begin)throw "
        "Error('Missing production archive helper');\nconst nodes=new "
        "Map([['/',{mode:1}],['/tmp',{mode:1}],['/project',{mode:1}]]);\nconst "
        "FS={\n lstat:path=>{const node=nodes.get(path);if(!node)throw "
        "Error('missing');return{mode:node.mode,size:node.data?.length||0};},"
        "\n isDir:mode=>mode===1,isFile:mode=>mode===2,\n "
        "analyzePath:path=>({exists:nodes.has(path)}),\n "
        "readdir:path=>['.','..',...new "
        "Set([...nodes.keys()].filter(name=>name.startsWith(path+'/"
        "')).map(name=>name.slice(path.length+1).split('/')[0]))],\n "
        "readFile:path=>nodes.get(path).data.slice(),\n "
        "mkdir:path=>{if(nodes.has(path))throw "
        "Error('exists');nodes.set(path,{mode:1});},\n mkdirTree:path=>{let "
        "current='';for(const part of "
        "path.split('/').filter(Boolean)){current+='/"
        "'+part;if(!nodes.has(current))nodes.set(current,{mode:1});else "
        "if(nodes.get(current).mode!==1)throw Error('not-directory');}},\n "
        "writeFile:(path,data,options)=>{if(options?.flags==='wx'&&nodes.has("
        "path))throw "
        "Error('exists');nodes.set(path,{mode:2,data:data.slice()});},\n "
        "unlink:path=>nodes.delete(path),rmdir:path=>nodes.delete(path)\n};"
        "\nconst encoder=new "
        "TextEncoder(),Module={};\nFS.writeFile('/project/"
        "project.kdl',encoder.encode('disk'));\nFS.writeFile('/project/"
        "'+String.fromCodePoint(0xe000),encoder.encode('bmp'));\nFS.writeFile('"
        "/project/"
        "'+String.fromCodePoint(0x10000),encoder.encode('astral'));"
        "\nrunInNewContext('(function(){'+helperSource.slice(begin,end)+'})()',"
        "\n "
        "{Module,FS,TextEncoder,TextDecoder,Uint8Array,Uint32Array,DataView,"
        "Blob,btoa,atob,crypto:webc",
        "rypto});\nconst helper=Module.browserWorkspaceFiles,stateBytes=new "
        "Uint8Array(Buffer.from(base.archive.attachments[0].base64,'base64'));"
        "\nconst "
        "capturedPromise=helper.capture('/"
        "project',[],[{name:'workspace-documents-v1',data:stateBytes}]);\nFS."
        "writeFile('/project/"
        "project.kdl',encoder.encode('later'));stateBytes[0]=0;\nconst "
        "captured=await capturedPromise;\nconst root=await "
        "helper.install(captured);\nconst "
        "actual=helper.attachment(root,'workspace-documents-v1');\nif(new "
        "TextDecoder().decode(FS.readFile(root+'/"
        "project.kdl'))!=='disk'||actual?.[0]!==67)throw Error('Archive did "
        "not freeze original disk and native document "
        "state');\ncodes.push('archive-roundtrip');\nactual[0]=0;if(helper."
        "attachment(root,'workspace-documents-v1')[0]!==67)throw "
        "Error('Borrowed mutable archive "
        "metadata');codes.push('archive-copy-owned');\nif(captured.files[1]."
        "path!==String.fromCodePoint(0xe000)||captured.files[2].path!==String."
        "fromCodePoint(0x10000))throw Error('Archive order differs from "
        "UTF8');codes.push('archive-utf8-sorted');\nhelper.remove(root);if("
        "nodes.has(root)||helper.attachment(root,'workspace-documents-v1')!=="
        "null)throw Error('Cancelled root metadata "
        "leaked');codes.push('archive-remove');\nconst adopted=await "
        "helper.install(captured);const "
        "beforeAdopt=helper.attachment(adopted,'workspace-documents-v1');"
        "helper.adopt(adopted);\nlet "
        "protectedRoot=false;try{helper.remove(adopted)}catch{protectedRoot="
        "true}\nif(!protectedRoot||!nodes.has(adopted)||beforeAdopt[0]!==67||"
        "helper.attachment(adopted,'workspace-documents-v1')!==null)throw "
        "Error('Adoption lost ownership "
        "boundary');codes.push('archive-adopt-protected');\nif(helper.release('"
        "/project',null)||helper.release(adopted,adopted+'/.' "
        ")||!nodes.has('/project')||!nodes.has(adopted))throw Error('Foreign "
        "or retained installation "
        "removed');codes.push('archive-release-guard');\nif(!helper.release("
        "adopted,null)||nodes.has(adopted)||helper.release(adopted,null))throw "
        "Error('Adopted retirement failed');\nfor(let i=0;i<3;i++){const "
        "temporary=await "
        "helper.install(captured);helper.adopt(temporary);if(!helper.re",
        "lease(temporary,null)||nodes.has(temporary))throw Error('Repeated "
        "adoption leaked files');}codes.push('archive-release-repeat');\nconst "
        "rehash=value=>value.sha256=createHash('sha256').update(JSON.stringify("
        "{files:value.files,attachments:value.attachments}),'utf8').digest('"
        "hex');\nconst reject=async(change,recompute=false)=>{const "
        "value=structuredClone(captured);change(value);if(recompute)rehash("
        "value);try{await "
        "helper.install(value);codes.push('unexpected-install')}catch{codes."
        "push('archive-refused')}};\nawait "
        "reject(value=>value.attachments[0].sha256='0'.repeat(64),true);"
        "\nawait reject(value=>value.files[0].base64='eB==');\nawait "
        "reject(value=>value.files[0].path='../bad');\nawait "
        "reject(value=>value.attachments.push({...value.attachments[0]}));"
        "\nawait "
        "reject(value=>value.attachments[0].bytes=33554433);\nnodes.set('/"
        "project/link',{mode:3});\ntry{await "
        "helper.capture('/"
        "project');codes.push('unexpected-link')}catch{codes.push('archive-"
        "refused')}\nconsole.log(JSON.stringify(codes));",
    };
    char script_path[1200];
    snprintf(script_path, sizeof script_path, "%s/checkpoint.mjs", test_directory);
    FILE *script_file = fopen(script_path, "wb");
    REQUIRE(script_file);
    bool wrote = true;
    for (size_t i = 0; i < sizeof script_parts / sizeof *script_parts; i++) {
        if (fputs(script_parts[i], script_file) == EOF) {
            wrote = false;
        }
    }
    if (fclose(script_file)) {
        wrote = false;
    }
    REQUIRE(wrote);
    const char *argv[] = { "node", script_path, input, NULL };
    int status = tool_run(argv, tool_root(), log);
    char *output = tool_read(log);
    yyjson_doc *doc = output ? yyjson_read(output, strlen(output), 0) : NULL;
    yyjson_val *codes = yyjson_doc_get_root(doc);
    static const char *const expected[] = {
        "ok",
        "unsorted_snapshot_files",
        "invalid_snapshot_file",
        "invalid_snapshot_file_encoding",
        "snapshot_file_hash_mismatch",
        "snapshot_byte_count_mismatch",
        "snapshot_archive_hash_mismatch",
        "snapshot_hash_mismatch",
        "invalid_snapshot",
        "invalid_workspace",
        "invalid_snapshot_attachments",
        "invalid_snapshot_attachments",
        "invalid_snapshot_documents",
        "invalid_snapshot_documents_encoding",
        "invalid_snapshot_documents",
        "snapshot_documents_hash_mismatch",
        "invalid_snapshot_documents_encoding",
        "unsupported_protocol",
        "ok",
        "invalid_document_edits",
        "invalid_document_edits",
        "invalid_document_edits",
        "invalid_payload_keys",
        "invalid_payload_keys",
        "invalid_document_edits",
        "invalid_document_edits",
        "invalid_document_edits",
        "action_too_large",
        "invalid_document_edits",
        "archive-roundtrip",
        "archive-copy-owned",
        "archive-utf8-sorted",
        "archive-remove",
        "archive-adopt-protected",
        "archive-release-guard",
        "archive-release-repeat",
        "archive-refused",
        "archive-refused",
        "archive-refused",
        "archive-refused",
        "archive-refused",
        "archive-refused"
    };
    bool passed = status == 0 &&
                  yyjson_arr_size(codes) == sizeof expected / sizeof *expected;
    for (size_t index = 0; passed && index < sizeof expected / sizeof *expected;
         index++) {
        passed = yyjson_equals_str(yyjson_arr_get(codes, index), expected[index]);
    }
    if (!passed && output) {
        fputs(output, stderr);
    }
    yyjson_doc_free(doc);
    free(output);
    tool_remove_tree(test_directory);
    REQUIRE(passed);
    return true;
}

void test_tool_collab()
{
    pu_setup(NULL, NULL);
    RUN_TEST_CASE(authored_checkpoint_v3_preserves_server_validation);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(room_creation_and_invites_require_the_correct_authority);
    pu_setup(NULL, NULL);
}
