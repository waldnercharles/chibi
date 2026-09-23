#include "editor/commands/codex_auth.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void set_text(char *target, size_t size, const char *text)
{
    if (size) {
        snprintf(target, size, "%s", text ? text : "");
    }
}

#ifdef __EMSCRIPTEN__

struct editor_codex_login
{
    int unused;
};

editor_codex_login *editor_codex_login_start(char *error, size_t error_size)
{
    set_text(error, error_size, "ChatGPT sign-in is unavailable in browser builds");
    return NULL;
}

bool editor_codex_login_poll(editor_codex_login *login, bool *success, char *message, size_t message_size)
{
    (void)login;
    if (success) {
        *success = false;
    }
    set_text(message, message_size, "ChatGPT sign-in is unavailable in browser builds");
    return true;
}

void editor_codex_login_free(editor_codex_login *login)
{
    (void)login;
}

bool editor_codex_authenticated()
{
    return false;
}

bool editor_codex_token(char **access, char **account_id, char *error, size_t error_size)
{
    if (access) {
        *access = NULL;
    }
    if (account_id) {
        *account_id = NULL;
    }
    set_text(error, error_size, "ChatGPT sign-in is unavailable in browser builds");
    return false;
}

void editor_codex_sign_out() {}

#else

#include "support/xalloc.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <yyjson.h>

#ifndef CHIBI_CODEX_CLIENT_ID
#define CHIBI_CODEX_CLIENT_ID "app_EMoamEEZ73f0CkXaXp7hrann"
#endif

#define CODEX_CALLBACK_PORT 1455
#define CODEX_LOGIN_TIMEOUT_MS 120000
#define CODEX_HTTP_TIMEOUT_SECONDS 15L
#define CODEX_RESPONSE_LIMIT (64 * 1024)
#define CODEX_CREDENTIAL_LIMIT (64 * 1024)
#define CODEX_ERROR_SIZE 192
#define CODEX_REDIRECT_URI "http://localhost:1455/auth/callback"
#define CODEX_TOKEN_URL "https://auth.openai.com/oauth/token"
#define CODEX_JWT_AUTH_CLAIM "https://api.openai.com/auth"

/* The browser OAuth endpoints and client identifier are undocumented. */

typedef struct
{
    char *access;
    char *refresh;
    char *account_id;
    uint64_t expires_at;
} codex_credentials;

typedef struct
{
    char *data;
    size_t length;
    bool too_large;
} response_buffer;

struct editor_codex_login
{
    int listener;
    int client;
    SDL_Thread *worker;
    SDL_AtomicInt worker_done;
    SDL_AtomicInt worker_cancelled;
    bool worker_success;
    char worker_message[CODEX_ERROR_SIZE];
    Uint64 started;
    char verifier[128];
    char challenge[128];
    char state[33];
    char code[4096];
    char request[8192];
    size_t request_length;
    bool complete;
    bool success;
    char message[CODEX_ERROR_SIZE];
};

static void clear_string(char **value)
{
    if (!*value) {
        return;
    }
    OPENSSL_cleanse(*value, strlen(*value));
    free(*value);
    *value = NULL;
}

static void clear_credentials(codex_credentials *credentials)
{
    clear_string(&credentials->access);
    clear_string(&credentials->refresh);
    clear_string(&credentials->account_id);
    OPENSSL_cleanse(credentials, sizeof *credentials);
}

static char *base64url(const unsigned char *data, size_t length)
{
    size_t capacity = 4 * ((length + 2) / 3) + 1;
    unsigned char *encoded = xmalloc(capacity);
    int encoded_length = EVP_EncodeBlock(encoded, data, (int)length);
    if (encoded_length < 0) {
        free(encoded);
        return NULL;
    }
    while (encoded_length > 0 && encoded[encoded_length - 1] == '=') {
        encoded_length--;
    }
    for (int i = 0; i < encoded_length; i++) {
        if (encoded[i] == '+') {
            encoded[i] = '-';
        }
        if (encoded[i] == '/') {
            encoded[i] = '_';
        }
    }
    encoded[encoded_length] = 0;
    return (char *)encoded;
}

static bool make_pkce(char verifier[128], char challenge[128])
{
    unsigned char random[32], digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;
    char *encoded = NULL;
    if (RAND_bytes(random, sizeof random) != 1) {
        return false;
    }
    encoded = base64url(random, sizeof random);
    OPENSSL_cleanse(random, sizeof random);
    if (!encoded || strlen(encoded) >= 128) {
        free(encoded);
        return false;
    }
    snprintf(verifier, 128, "%s", encoded);
    OPENSSL_cleanse(encoded, strlen(encoded));
    free(encoded);
    if (EVP_Digest(verifier, strlen(verifier), digest, &digest_length, EVP_sha256(), NULL) !=
        1) {
        OPENSSL_cleanse(digest, sizeof digest);
        return false;
    }
    encoded = base64url(digest, digest_length);
    OPENSSL_cleanse(digest, sizeof digest);
    if (!encoded || strlen(encoded) >= 128) {
        free(encoded);
        return false;
    }
    snprintf(challenge, 128, "%s", encoded);
    free(encoded);
    return true;
}

static bool make_state(char state[33])
{
    unsigned char random[16];
    static const char hex[] = "0123456789abcdef";
    if (RAND_bytes(random, sizeof random) != 1) {
        return false;
    }
    for (size_t i = 0; i < sizeof random; i++) {
        state[i * 2] = hex[random[i] >> 4];
        state[i * 2 + 1] = hex[random[i] & 15];
    }
    state[32] = 0;
    OPENSSL_cleanse(random, sizeof random);
    return true;
}

static char *credential_path(char *error, size_t error_size)
{
    char *directory = SDL_GetPrefPath("Chibi", "Chibi");
    if (!directory) {
        set_text(error, error_size, "Cannot access the ChatGPT credentials directory");
        return NULL;
    }
    if (chmod(directory, 0700) != 0) {
        SDL_free(directory);
        set_text(error, error_size, "Cannot secure the ChatGPT credentials directory");
        return NULL;
    }
    size_t length = strlen(directory) + sizeof "codex-auth.json";
    char *path = xmalloc(length);
    snprintf(path, length, "%scodex-auth.json", directory);
    SDL_free(directory);
    return path;
}

static bool credential_read(codex_credentials *credentials, char *error, size_t error_size)
{
    char *path = credential_path(error, error_size);
    if (!path) {
        return false;
    }
    int flags = O_RDONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(path, flags);
    free(path);
    if (fd < 0) {
        set_text(error, error_size, errno == ENOENT ? "Sign in to ChatGPT" : "Cannot read ChatGPT credentials");
        return false;
    }
    struct stat status;
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 0 ||
        (uint64_t)status.st_size > CODEX_CREDENTIAL_LIMIT || fchmod(fd, 0600) != 0) {
        close(fd);
        set_text(error, error_size, "ChatGPT credentials are invalid");
        return false;
    }
    FILE *file = fdopen(fd, "rb");
    if (!file) {
        close(fd);
        set_text(error, error_size, "Cannot read ChatGPT credentials");
        return false;
    }
    size_t capacity = (size_t)status.st_size + 1;
    char *json = xmalloc(capacity);
    size_t length = fread(json, 1, capacity - 1, file);
    bool read_ok = !ferror(file) && length == (size_t)status.st_size;
    fclose(file);
    json[length] = 0;
    yyjson_doc *doc = read_ok ? yyjson_read(json, length, 0) : NULL;
    OPENSSL_cleanse(json, capacity);
    free(json);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *access = yyjson_is_obj(root) ? yyjson_obj_get(root, "access_token")
                                             : NULL;
    yyjson_val *refresh = yyjson_is_obj(root) ? yyjson_obj_get(root, "refresh_token")
                                              : NULL;
    yyjson_val *account = yyjson_is_obj(root) ? yyjson_obj_get(root, "account_id")
                                              : NULL;
    yyjson_val *expires = yyjson_is_obj(root) ? yyjson_obj_get(root, "expires_at")
                                              : NULL;
    bool valid = yyjson_is_str(access) && yyjson_get_len(access) &&
                 yyjson_is_str(refresh) && yyjson_get_len(refresh) &&
                 yyjson_is_str(account) && yyjson_get_len(account) &&
                 (yyjson_is_uint(expires) || yyjson_is_sint(expires));
    if (valid) {
        int64_t expiry = yyjson_get_sint(expires);
        valid = expiry > 0;
        if (valid) {
            credentials->access = xstrdup(yyjson_get_str(access));
            credentials->refresh = xstrdup(yyjson_get_str(refresh));
            credentials->account_id = xstrdup(yyjson_get_str(account));
            credentials->expires_at = (uint64_t)expiry;
        }
    }
    yyjson_doc_free(doc);
    if (!valid) {
        set_text(error, error_size, "ChatGPT credentials are invalid; sign in again");
        return false;
    }
    return true;
}

static bool credential_write(const codex_credentials *credentials, char *error, size_t error_size)
{
    char *path = credential_path(error, error_size);
    if (!path) {
        return false;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "access_token", credentials->access);
    yyjson_mut_obj_add_strcpy(doc, root, "refresh_token", credentials->refresh);
    yyjson_mut_obj_add_strcpy(doc, root, "account_id", credentials->account_id);
    yyjson_mut_obj_add_uint(doc, root, "expires_at", credentials->expires_at);
    size_t json_length = 0;
    char *json = yyjson_mut_write(doc, 0, &json_length);
    yyjson_mut_doc_free(doc);
    if (!json) {
        free(path);
        set_text(error, error_size, "Cannot save ChatGPT credentials");
        return false;
    }
    size_t temporary_length = strlen(path) + sizeof ".XXXXXX";
    char *temporary = xmalloc(temporary_length);
    snprintf(temporary, temporary_length, "%s.XXXXXX", path);
    int fd = mkstemp(temporary);
    bool ok = fd >= 0 && fchmod(fd, 0600) == 0;
    size_t written = 0;
    while (ok && written < json_length) {
        ssize_t count = write(fd, json + written, json_length - written);
        if (count <= 0) {
            ok = false;
            break;
        }
        written += (size_t)count;
    }
    if (ok && fsync(fd) != 0) {
        ok = false;
    }
    if (fd >= 0 && close(fd) != 0) {
        ok = false;
    }
    if (ok && rename(temporary, path) != 0) {
        ok = false;
    }
    if (!ok && fd >= 0) {
        unlink(temporary);
    }
    OPENSSL_cleanse(json, json_length);
    free(json);
    free(temporary);
    free(path);
    if (!ok) {
        set_text(error, error_size, "Cannot save ChatGPT credentials");
    }
    return ok;
}

static char *account_id_from_access(const char *access)
{
    const char *first_dot = strchr(access, '.');
    const char *second_dot = first_dot ? strchr(first_dot + 1, '.') : NULL;
    if (!first_dot || !second_dot || second_dot == first_dot + 1) {
        return NULL;
    }
    size_t encoded_length = (size_t)(second_dot - first_dot - 1);
    size_t padded_length = (encoded_length + 3) & ~(size_t)3;
    unsigned char *encoded = xmalloc(padded_length + 1);
    memcpy(encoded, first_dot + 1, encoded_length);
    for (size_t i = 0; i < encoded_length; i++) {
        if (encoded[i] == '-') {
            encoded[i] = '+';
        }
        if (encoded[i] == '_') {
            encoded[i] = '/';
        }
    }
    memset(encoded + encoded_length, '=', padded_length - encoded_length);
    encoded[padded_length] = 0;
    size_t decoded_capacity = (padded_length / 4) * 3 + 1;
    unsigned char *decoded = xmalloc(decoded_capacity);
    int decoded_length = EVP_DecodeBlock(decoded, encoded, (int)padded_length);
    free(encoded);
    if (decoded_length < 0) {
        free(decoded);
        return NULL;
    }
    decoded_length -= (int)(padded_length - encoded_length);
    if (decoded_length < 0) {
        OPENSSL_cleanse(decoded, decoded_capacity);
        free(decoded);
        return NULL;
    }
    yyjson_doc *doc = yyjson_read((const char *)decoded, (size_t)decoded_length, 0);
    OPENSSL_cleanse(decoded, decoded_capacity);
    free(decoded);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *auth = yyjson_is_obj(root)
                         ? yyjson_obj_get(root, CODEX_JWT_AUTH_CLAIM)
                         : NULL;
    yyjson_val *account = yyjson_is_obj(auth)
                            ? yyjson_obj_get(auth, "chatgpt_account_id")
                            : NULL;
    char *result = yyjson_is_str(account) && yyjson_get_len(account)
                     ? xstrdup(yyjson_get_str(account))
                     : NULL;
    yyjson_doc_free(doc);
    return result;
}

static size_t collect_response(char *data, size_t size, size_t count, void *context)
{
    response_buffer *response = context;
    if (size && count > SIZE_MAX / size) {
        response->too_large = true;
        return 0;
    }
    size_t length = size * count;
    if (length > CODEX_RESPONSE_LIMIT - response->length) {
        response->too_large = true;
        return 0;
    }
    response->data = xrealloc(response->data, response->length + length + 1);
    memcpy(response->data + response->length, data, length);
    response->length += length;
    response->data[response->length] = 0;
    return length;
}

static bool curl_ready()
{
    static bool initialized;
    if (initialized) {
        return true;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return false;
    }
    initialized = true;
    return true;
}

static char *form_escape(CURL *curl, const char *value)
{
    return curl_easy_escape(curl, value, 0);
}

static bool form_append(
    CURL *curl,
    char *body,
    size_t body_size,
    size_t *length,
    const char *key,
    const char *value
)
{
    char *encoded = form_escape(curl, value);
    if (!encoded) {
        return false;
    }
    int count = snprintf(body + *length, body_size - *length, "%s%s=%s", *length ? "&" : "", key, encoded);
    curl_free(encoded);
    if (count < 0 || (size_t)count >= body_size - *length) {
        return false;
    }
    *length += (size_t)count;
    return true;
}

static int cancel_curl(
    void *context,
    curl_off_t download_total,
    curl_off_t download_now,
    curl_off_t upload_total,
    curl_off_t upload_now
)
{
    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    SDL_AtomicInt *cancelled = context;
    return SDL_GetAtomicInt(cancelled) != 0;
}

static bool request_token(
    const char *body,
    codex_credentials *credentials,
    SDL_AtomicInt *cancelled,
    const char *fallback_refresh,
    char *error,
    size_t error_size
)
{
    if (!curl_ready()) {
        set_text(error, error_size, "Cannot initialize ChatGPT sign-in");
        return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        set_text(error, error_size, "Cannot initialize ChatGPT sign-in");
        return false;
    }
    response_buffer response = { 0 };
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "Expect:");
    curl_easy_setopt(curl, CURLOPT_URL, CODEX_TOKEN_URL);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)strlen(body));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, CODEX_HTTP_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CODEX_HTTP_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    if (cancelled) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, cancel_curl);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancelled);
    }
    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || status < 200 || status >= 300 ||
        response.too_large || !response.data) {
        if (response.data) {
            OPENSSL_cleanse(response.data, response.length);
            free(response.data);
        }
        set_text(error, error_size, "ChatGPT sign-in request failed");
        return false;
    }
    yyjson_doc *doc = yyjson_read(response.data, response.length, 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *access = yyjson_is_obj(root) ? yyjson_obj_get(root, "access_token")
                                             : NULL;
    yyjson_val *refresh = yyjson_is_obj(root) ? yyjson_obj_get(root, "refresh_token")
                                              : NULL;
    yyjson_val *expires = yyjson_is_obj(root) ? yyjson_obj_get(root, "expires_in")
                                              : NULL;
    bool valid_expiry = yyjson_is_uint(expires) || yyjson_is_sint(expires);
    int64_t expires_in = valid_expiry ? yyjson_get_sint(expires) : 0;
    valid_expiry = valid_expiry && expires_in > 0 && expires_in <= 31536000;
    bool has_refresh = yyjson_is_str(refresh) && yyjson_get_len(refresh);
    bool valid = yyjson_is_str(access) && yyjson_get_len(access) &&
                 (has_refresh || fallback_refresh) && valid_expiry;
    if (valid) {
        credentials->access = xstrdup(yyjson_get_str(access));
        credentials->refresh = xstrdup(has_refresh ? yyjson_get_str(refresh) : fallback_refresh);
        credentials->account_id = account_id_from_access(credentials->access);
        time_t now = time(NULL);
        valid = credentials->account_id && now > 0 &&
                (uint64_t)now <= UINT64_MAX - (uint64_t)expires_in;
        if (valid) {
            credentials->expires_at = (uint64_t)now + (uint64_t)expires_in;
        }
    }
    yyjson_doc_free(doc);
    OPENSSL_cleanse(response.data, response.length);
    free(response.data);
    if (!valid) {
        clear_credentials(credentials);
        set_text(error, error_size, "ChatGPT sign-in response was invalid");
        return false;
    }
    return true;
}

static bool make_form(
    char *body,
    size_t body_size,
    const char *grant,
    const char *credential,
    const char *credential_value,
    const char *verifier,
    char *error,
    size_t error_size
)
{
    if (!curl_ready()) {
        set_text(error, error_size, "Cannot initialize ChatGPT sign-in");
        return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        set_text(error, error_size, "Cannot initialize ChatGPT sign-in");
        return false;
    }
    size_t length = 0;
    bool ok = form_append(curl, body, body_size, &length, "grant_type", grant) &&
              form_append(curl, body, body_size, &length, "client_id", CHIBI_CODEX_CLIENT_ID) &&
              form_append(curl, body, body_size, &length, credential, credential_value);
    if (ok && verifier) {
        ok = form_append(curl, body, body_size, &length, "code_verifier", verifier) &&
             form_append(curl, body, body_size, &length, "redirect_uri", CODEX_REDIRECT_URI);
    }
    curl_easy_cleanup(curl);
    if (!ok) {
        OPENSSL_cleanse(body, body_size);
        set_text(error, error_size, "Cannot prepare ChatGPT sign-in request");
        return false;
    }
    return true;
}

static bool exchange_code(
    const char *code,
    const char *verifier,
    codex_credentials *credentials,
    SDL_AtomicInt *cancelled,
    char *error,
    size_t error_size
)
{
    char body[16384] = { 0 };
    bool ok = make_form(body, sizeof body, "authorization_code", "code", code, verifier, error, error_size);
    if (ok) {
        ok = request_token(body, credentials, cancelled, NULL, error, error_size);
    }
    OPENSSL_cleanse(body, sizeof body);
    return ok;
}

static int SDLCALL exchange_login_thread(void *context)
{
    editor_codex_login *login = context;
    codex_credentials credentials = { 0 };
    bool ok = exchange_code(
        login->code,
        login->verifier,
        &credentials,
        &login->worker_cancelled,
        login->worker_message,
        sizeof login->worker_message
    );
    if (ok) {
        ok = credential_write(&credentials, login->worker_message, sizeof login->worker_message);
    }
    clear_credentials(&credentials);
    login->worker_success = ok;
    if (SDL_GetAtomicInt(&login->worker_cancelled)) {
        set_text(login->worker_message, sizeof login->worker_message, "Sign-in cancelled");
        login->worker_success = false;
    } else if (ok) {
        set_text(login->worker_message, sizeof login->worker_message, "ChatGPT sign-in complete");
    }
    SDL_SetAtomicInt(&login->worker_done, 1);
    return 0;
}

static bool refresh_token(const char *refresh, codex_credentials *credentials, char *error, size_t error_size)
{
    char body[16384] = { 0 };
    bool ok = make_form(body, sizeof body, "refresh_token", "refresh_token", refresh, NULL, error, error_size);
    if (ok) {
        ok = request_token(body, credentials, NULL, refresh, error, error_size);
    }
    OPENSSL_cleanse(body, sizeof body);
    return ok;
}

static bool open_callback_listener(int *listener)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(CODEX_CALLBACK_PORT),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    if (bind(fd, (struct sockaddr *)&address, sizeof address) != 0 ||
        listen(fd, 4) != 0) {
        close(fd);
        return false;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(fd);
        return false;
    }
    *listener = fd;
    return true;
}

static bool authorize_url(const editor_codex_login *login, char *url, size_t size)
{
    int written = snprintf(
        url,
        size,
        "https://auth.openai.com/oauth/"
        "authorize?response_type=code&client_id=%s&redirect_uri=http%%3A%%2F%%"
        "2Flocalhost%%3A1455%%2Fauth%%2Fcallback&scope=openid%%20profile%%"
        "20email%%20offline_access&code_challenge=%s&code_challenge_method="
        "S256&state=%s&id_token_add_organizations=true&codex_cli_simplified_"
        "flow=true&originator=chibi",
        CHIBI_CODEX_CLIENT_ID,
        login->challenge,
        login->state
    );
    return written > 0 && (size_t)written < size;
}

static bool send_callback_response(int client, int status, const char *reason, const char *body)
{
    char response[1024];
    int length = snprintf(
        response,
        sizeof response,
        "HTTP/1.1 %d %s\r\nContent-Type: text/html; "
        "charset=utf-8\r\nCache-Control: no-store\r\nConnection: "
        "close\r\nContent-Length: %zu\r\n\r\n%s",
        status,
        reason,
        strlen(body),
        body
    );
    if (length <= 0 || (size_t)length >= sizeof response) {
        return false;
    }
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled);
#endif
#ifdef MSG_NOSIGNAL
    int flags = MSG_NOSIGNAL;
#else
    int flags = 0;
#endif
    size_t sent = 0;
    while (sent < (size_t)length) {
        ssize_t count = send(client, response + sent, (size_t)length - sent, flags);
        if (count <= 0) {
            return false;
        }
        sent += (size_t)count;
    }
    return true;
}

static bool decode_query_value(const char *value, size_t length, char *output, size_t output_size)
{
    size_t written = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)value[i];
        if (ch == '%' && i + 2 < length) {
            int high = isdigit((unsigned char)value[i + 1])
                         ? value[i + 1] - '0'
                         : (tolower((unsigned char)value[i + 1]) - 'a' + 10);
            int low = isdigit((unsigned char)value[i + 2])
                        ? value[i + 2] - '0'
                        : (tolower((unsigned char)value[i + 2]) - 'a' + 10);
            if (high < 0 || high > 15 || low < 0 || low > 15 ||
                (!isdigit((unsigned char)value[i + 1]) &&
                 !isalpha((unsigned char)value[i + 1])) ||
                (!isdigit((unsigned char)value[i + 2]) &&
                 !isalpha((unsigned char)value[i + 2]))) {
                return false;
            }
            ch = (unsigned char)((high << 4) | low);
            i += 2;
        } else if (ch == '%') {
            return false;
        } else if (ch == '+') {
            ch = ' ';
        }
        if (!ch || written + 1 >= output_size) {
            return false;
        }
        output[written++] = (char)ch;
    }
    output[written] = 0;
    return true;
}

static bool query_fields(
    const char *query,
    char *code,
    size_t code_size,
    char *state,
    size_t state_size,
    char *oauth_error,
    size_t error_size
)
{
    while (query && *query) {
        const char *end = strchr(query, '&');
        size_t length = end ? (size_t)(end - query) : strlen(query);
        const char *equals = memchr(query, '=', length);
        if (equals) {
            size_t key_length = (size_t)(equals - query);
            const char *value = equals + 1;
            size_t value_length = length - key_length - 1;
            bool ok = true;
            if (key_length == 4 && !memcmp(query, "code", 4)) {
                ok = decode_query_value(value, value_length, code, code_size);
            }
            if (key_length == 5 && !memcmp(query, "state", 5)) {
                ok = decode_query_value(value, value_length, state, state_size);
            }
            if (key_length == 5 && !memcmp(query, "error", 5)) {
                ok = decode_query_value(value, value_length, oauth_error, error_size);
            }
            if (!ok) {
                return false;
            }
        }
        query = end ? end + 1 : NULL;
    }
    return true;
}

/* Return 0 while pending, 1 when a code arrived, and -1 for a completed error. */
static int receive_callback(editor_codex_login *login)
{
    if (login->client < 0) {
        struct sockaddr_in address;
        socklen_t address_size = sizeof address;
        login->client = accept(login->listener, (struct sockaddr *)&address, &address_size);
        if (login->client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return 0;
            }
            set_text(login->message, sizeof login->message, "Sign-in callback failed");
            return -1;
        }
        int flags = fcntl(login->client, F_GETFL, 0);
        if (flags < 0 || fcntl(login->client, F_SETFL, flags | O_NONBLOCK) != 0) {
            close(login->client);
            login->client = -1;
            return 0;
        }
    }
    ssize_t count = recv(
        login->client,
        login->request + login->request_length,
        sizeof login->request - login->request_length - 1,
        0
    );
    if (count == 0) {
        close(login->client);
        login->client = -1;
        login->request_length = 0;
        return 0;
    }
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        close(login->client);
        login->client = -1;
        login->request_length = 0;
        return 0;
    }
    login->request_length += (size_t)count;
    login->request[login->request_length] = 0;
    char *line_end = strstr(login->request, "\r\n");
    if (!line_end && login->request_length < sizeof login->request - 1) {
        return 0;
    }
    char *question = NULL;
    bool request_ok = line_end && !strncmp(login->request, "GET ", 4);
    if (request_ok) {
        char *url = login->request + 4;
        char *space = memchr(url, ' ', (size_t)(line_end - url));
        request_ok = space != NULL;
        if (request_ok) {
            *space = 0;
            question = strchr(url, '?');
            request_ok = question &&
                         (size_t)(question - url) == strlen("/auth/callback") &&
                         !memcmp(url, "/auth/callback", strlen("/auth/callback"));
        }
    }
    if (!request_ok) {
        send_callback_response(login->client, 404, "Not Found", "<!doctype html><title>Chibi</title><p>Sign-in callback not found.</p>");
        close(login->client);
        login->client = -1;
        login->request_length = 0;
        return 0;
    }
    char code[4096] = { 0 }, state[128] = { 0 }, oauth_error[256] = { 0 };
    if (!query_fields(question + 1, code, sizeof code, state, sizeof state, oauth_error, sizeof oauth_error)) {
        send_callback_response(login->client, 400, "Bad Request", "<!doctype html><title>Chibi</title><p>Invalid sign-in response.</p>");
        close(login->client);
        login->client = -1;
        login->request_length = 0;
        set_text(login->message, sizeof login->message, "Invalid sign-in response");
        return -1;
    }
    bool state_matches = strlen(state) == strlen(login->state) &&
                         CRYPTO_memcmp(state, login->state, strlen(login->state)) == 0;
    if (!state_matches) {
        send_callback_response(login->client, 400, "Bad Request", "<!doctype html><title>Chibi</title><p>Sign-in state did not match.</p>");
        close(login->client);
        login->client = -1;
        login->request_length = 0;
        OPENSSL_cleanse(code, sizeof code);
        OPENSSL_cleanse(state, sizeof state);
        return 0;
    }
    if (oauth_error[0]) {
        send_callback_response(login->client, 400, "Bad Request", "<!doctype html><title>Chibi</title><p>Sign-in was cancelled.</p>");
        set_text(login->message, sizeof login->message, "Sign-in was cancelled");
        close(login->client);
        login->client = -1;
        login->request_length = 0;
        OPENSSL_cleanse(code, sizeof code);
        OPENSSL_cleanse(state, sizeof state);
        OPENSSL_cleanse(oauth_error, sizeof oauth_error);
        return -1;
    }
    if (!code[0]) {
        send_callback_response(login->client, 400, "Bad Request", "<!doctype html><title>Chibi</title><p>Sign-in response was incomplete.</p>");
        set_text(login->message, sizeof login->message, "Sign-in response was incomplete");
        close(login->client);
        login->client = -1;
        login->request_length = 0;
        OPENSSL_cleanse(state, sizeof state);
        return -1;
    }
    snprintf(login->code, sizeof login->code, "%s", code);
    send_callback_response(login->client, 200, "OK", "<!doctype html><title>Chibi</title><p>ChatGPT sign-in completed. You can close this window.</p>");
    close(login->client);
    login->client = -1;
    login->request_length = 0;
    OPENSSL_cleanse(code, sizeof code);
    OPENSSL_cleanse(state, sizeof state);
    return 1;
}

static void close_login_sockets(editor_codex_login *login)
{
    if (login->client >= 0) {
        close(login->client);
        login->client = -1;
    }
    if (login->listener >= 0) {
        close(login->listener);
        login->listener = -1;
    }
}

static void finish_login(editor_codex_login *login, bool success, const char *message)
{
    close_login_sockets(login);
    OPENSSL_cleanse(login->code, sizeof login->code);
    OPENSSL_cleanse(login->request, sizeof login->request);
    login->request_length = 0;
    login->complete = true;
    login->success = success;
    set_text(login->message, sizeof login->message, message);
}

editor_codex_login *editor_codex_login_start(char *error, size_t error_size)
{
    editor_codex_login *login = xmalloc(sizeof *login);
    memset(login, 0, sizeof *login);
    login->listener = -1;
    login->client = -1;
    if (!curl_ready() || !make_pkce(login->verifier, login->challenge) ||
        !make_state(login->state)) {
        editor_codex_login_free(login);
        set_text(error, error_size, "Cannot start ChatGPT sign-in");
        return NULL;
    }
    if (!open_callback_listener(&login->listener)) {
        editor_codex_login_free(login);
        set_text(error, error_size, "Cannot start sign-in callback on localhost:1455");
        return NULL;
    }
    char url[2048];
    if (!authorize_url(login, url, sizeof url) || !SDL_OpenURL(url)) {
        editor_codex_login_free(login);
        set_text(error, error_size, "Cannot open the ChatGPT sign-in page");
        return NULL;
    }
    OPENSSL_cleanse(url, sizeof url);
    login->started = SDL_GetTicks();
    set_text(error, error_size, "");
    return login;
}

bool editor_codex_login_poll(editor_codex_login *login, bool *success, char *message, size_t message_size)
{
    if (success) {
        *success = false;
    }
    if (!login) {
        set_text(message, message_size, "Sign-in was not started");
        return true;
    }
    if (login->complete) {
        if (success) {
            *success = login->success;
        }
        set_text(message, message_size, login->message);
        return true;
    }
    if (login->worker) {
        if (!SDL_GetAtomicInt(&login->worker_done)) {
            set_text(message, message_size, "Completing ChatGPT sign-in");
            return false;
        }
        SDL_WaitThread(login->worker, NULL);
        login->worker = NULL;
        finish_login(login, login->worker_success, login->worker_message);
    }
    if (login->complete) {
        if (success) {
            *success = login->success;
        }
        set_text(message, message_size, login->message);
        return true;
    }
    if (SDL_GetTicks() - login->started >= CODEX_LOGIN_TIMEOUT_MS) {
        finish_login(login, false, "ChatGPT sign-in timed out");
    } else {
        int callback = receive_callback(login);
        if (callback < 0) {
            finish_login(login, false, login->message[0] ? login->message : "Sign-in callback failed");
        }
        if (callback > 0) {
            close_login_sockets(login);
            login->worker = SDL_CreateThread(exchange_login_thread, "Codex sign-in", login);
            if (!login->worker) {
                finish_login(login, false, "Could not complete ChatGPT sign-in");
            }
        }
    }
    if (success) {
        *success = login->success;
    }
    set_text(
        message,
        message_size,
        login->complete ? login->message
                        : (login->worker ? "Completing ChatGPT sign-in"
                                         : "Waiting for browser sign-in")
    );
    return login->complete;
}

void editor_codex_login_free(editor_codex_login *login)
{
    if (!login) {
        return;
    }
    if (login->worker) {
        SDL_SetAtomicInt(&login->worker_cancelled, 1);
        SDL_WaitThread(login->worker, NULL);
    }
    close_login_sockets(login);
    OPENSSL_cleanse(login->verifier, sizeof login->verifier);
    OPENSSL_cleanse(login->challenge, sizeof login->challenge);
    OPENSSL_cleanse(login->state, sizeof login->state);
    OPENSSL_cleanse(login->code, sizeof login->code);
    OPENSSL_cleanse(login->request, sizeof login->request);
    free(login);
}

bool editor_codex_authenticated()
{
    codex_credentials credentials = { 0 };
    char error[CODEX_ERROR_SIZE] = { 0 };
    bool authenticated = credential_read(&credentials, error, sizeof error);
    clear_credentials(&credentials);
    return authenticated;
}

bool editor_codex_token(char **access, char **account_id, char *error, size_t error_size)
{
    if (access) {
        *access = NULL;
    }
    if (account_id) {
        *account_id = NULL;
    }
    codex_credentials credentials = { 0 };
    if (!credential_read(&credentials, error, error_size)) {
        return false;
    }
    time_t now = time(NULL);
    if (now <= 0) {
        clear_credentials(&credentials);
        set_text(error, error_size, "Cannot read system time for ChatGPT credentials");
        return false;
    }
    if (credentials.expires_at <= (uint64_t)now + 60) {
        codex_credentials refreshed = { 0 };
        char refresh_error[CODEX_ERROR_SIZE] = { 0 };
        bool ok = refresh_token(credentials.refresh, &refreshed, refresh_error, sizeof refresh_error);
        if (ok) {
            ok = credential_write(&refreshed, refresh_error, sizeof refresh_error);
        }
        if (!ok) {
            clear_credentials(&credentials);
            clear_credentials(&refreshed);
            set_text(error, error_size, "ChatGPT sign-in expired; sign in again");
            return false;
        }
        clear_credentials(&credentials);
        credentials = refreshed;
    }
    if (access) {
        *access = xstrdup(credentials.access);
    }
    if (account_id) {
        *account_id = xstrdup(credentials.account_id);
    }
    clear_credentials(&credentials);
    set_text(error, error_size, "");
    return true;
}

void editor_codex_sign_out()
{
    char error[CODEX_ERROR_SIZE] = { 0 };
    char *path = credential_path(error, sizeof error);
    if (!path) {
        return;
    }
    unlink(path);
    free(path);
}

#endif
