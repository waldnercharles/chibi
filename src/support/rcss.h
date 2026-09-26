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
 * `directory` is where imports resolve; NULL allows none. Appends each file
 * read to `imports` (support/array.h) when it is not NULL. Returns owned RCSS,
 * or NULL with a message in `error`. */
char *rcss_expand(const char *text, const char *directory, char ***imports, char *error, size_t error_size);

#endif
