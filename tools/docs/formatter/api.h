#ifndef CHIBI_DOCS_FORMATTER_API_H
#define CHIBI_DOCS_FORMATTER_API_H

/* Browser-only request budget; the shared formatter retains its own limits. */
#define CHIBI_FORMAT_WEB_MAX_BYTES (256 * 1024)

/* Returns malloc-owned formatted text, or NULL with a borrowed error string.
 * The caller owns source, including its trailing NUL. Requests are sequential.
 * Only the existing syntax formatter runs: user source is never executed. */
char *chibi_format_web(
    const char *source,
    int bytes,
    int indent_width,
    int preferred_width,
    int use_tabs,
    int remove_optional_semicolons
);
const char *chibi_format_web_error();
const char *chibi_format_web_revision();
int chibi_format_web_api();

#endif
