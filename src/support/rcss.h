#ifndef CHIBI_RCSS_H
#define CHIBI_RCSS_H

#include <stddef.h>

/* RCSS with the Sass conveniences RmlUi lacks, expanded to plain RCSS:
 *
 *   @import "controls.rcss";  the file beside the importing one, in place and
 *                             once per expansion
 *   .menu { .item { } }       nested rules flatten to `.menu .item`
 *   &:hover, .dark &          `&` stands for the enclosing selector
 *   .a { @media (x) { } }     a nested @media wraps the rules inside it
 *   // comment                line comments are dropped, like block comments
 *   :root { }                 names body, RmlUi's root, as CSS themes write it
 *
 * A rule's declarations come before its nested rules. @keyframes,
 * @decorator, @spritesheet and @font-face blocks pass through as written.
 * `directory` is where imports resolve; NULL leaves them as written, for
 * checking a stylesheet away from its files. Appends each file
 * read to `imports` (support/array.h) when it is not NULL. Returns owned RCSS,
 * or NULL with a message in `error`. */
char *rcss_expand(const char *text, const char *directory, char ***imports, char *error, size_t error_size);

/* A custom property as written, for an editor that changes its value in
 * place. */
typedef struct rcss_variable
{
    char selector[128]; /* of the innermost rule declaring it, spaces collapsed */
    char name[64];      /* with its leading -- */
    int start, end;     /* the value's bytes in the text, trimmed */
} rcss_variable;

/* Appends each custom property `text` declares to `variables`
 * (support/array.h), in order. */
void rcss_variables(const char *text, rcss_variable **variables);

#endif
