#ifndef CHIBI_RUNTIME_URL_DEFINITION_H
#define CHIBI_RUNTIME_URL_DEFINITION_H

#include "runtime/native_definitions.h"

#define URL_OPEN_NAME "Open URL"
#define URL_CANCEL_NAME "Cancel Open URL"
#define URL_PORT(X, NAME, KIND, STORAGE, TYPE)                                 \
    X(NAME,                                                                    \
      KIND,                                                                    \
      STORAGE,                                                                 \
      TYPE,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = STORAGE }))
#define URL_EXEC(X) URL_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)
#define URL_THEN(X) URL_PORT(X, "Then", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)
#define URL_OPEN_INPUTS(X)                                                     \
    URL_EXEC(X)                                                                \
    URL_PORT(X, "Url", NATIVE_PARAMETER_DATA, PIN_STR, NULL)
#define URL_OPEN_OUTPUTS(X)                                                    \
    URL_THEN(X)                                                                \
    URL_PORT(X, "Accepted", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool")
#define URL_OPEN_PRESENTATION                                                  \
    .category = "Host", .keywords = "browser website link external",           \
    .description = "Queues an HTTP(S) URL in an enabled player host. "         \
                   "Accepted "                                                 \
                   "means queued, not opened. The browser waits for trusted "  \
                   "input and may block the request. A later request "         \
                   "replaces it."
#define URL_CANCEL_PRESENTATION                                                \
    .category = "Host", .keywords = "browser website link external cancel",    \
    .description = "Cancels an external URL request that has not launched."

extern const native_definition_pack URL_DEFINITION_PACK;

#endif
