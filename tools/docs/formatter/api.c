#include "api.h"

#include "authoring/source_format.h"

#include <stdio.h>
#include <string.h>

#ifndef CHIBI_FORMAT_REVISION
#error "Build the browser formatter with its exact source revision"
#endif

static char last_error[512];

const char *chibi_format_web_error()
{
    return last_error;
}

const char *chibi_format_web_revision()
{
    return CHIBI_FORMAT_REVISION;
}

int chibi_format_web_api()
{
    return 1;
}

char *chibi_format_web(
    const char *source,
    int bytes,
    int indent_width,
    int preferred_width,
    int use_tabs,
    int remove_optional_semicolons
)
{
    last_error[0] = '\0';
    if (!source || bytes < 0 || bytes > CHIBI_FORMAT_WEB_MAX_BYTES ||
        source[bytes] != '\0' || memchr(source, '\0', (size_t)bytes)) {
        snprintf(last_error, sizeof last_error, "Expected at most 256 KiB of source without NUL bytes");
        return NULL;
    }
    if ((use_tabs != 0 && use_tabs != 1) ||
        (remove_optional_semicolons != 0 && remove_optional_semicolons != 1)) {
        snprintf(last_error, sizeof last_error, "Formatter switches must be true or false");
        return NULL;
    }
    authoring_format_options options = {
        .indent_width = indent_width,
        .preferred_width = preferred_width,
        .use_tabs = use_tabs != 0,
        .remove_optional_semicolons = remove_optional_semicolons != 0
    };
    script_syntax *syntax = script_parse(source);
    char *formatted = authoring_source_format_with_options(syntax, &options, NULL, last_error, sizeof last_error);
    script_syntax_free(syntax);
    return formatted;
}
