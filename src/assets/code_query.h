#ifndef CHIBI_CODE_QUERY_H
#define CHIBI_CODE_QUERY_H

#include "support/asset_value.h"
#include "support/code_lex.h"

/* Parses all/any/not/has query expressions. The document owns the result.
 * On success, current is the first token after the expression. Component
 * names are resolved by the system document's registry at load time. */
av_value *code_query_parse(av_doc *doc, code_lexer *lexer, code_token *current, char *error, size_t size);
/* A malloc-owned expression, without the query keyword or semicolon. */
char *code_query_write(const av_value *query);

#endif
