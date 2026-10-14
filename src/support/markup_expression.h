#ifndef CHIBI_MARKUP_EXPRESSION_H
#define CHIBI_MARKUP_EXPRESSION_H
/* Owned XML-decoded expression text with exact original byte origins for
 * every decoded byte. Unknown entities stay literal. Inputs are validated
 * source byte ranges; offsets are absolute in the original source. */
typedef struct
{
    char *text;
    int *starts, *ends, length;
} markup_expression;
void markup_expression_decode(const char *, int start, int end, markup_expression *);
void markup_expression_free(markup_expression *);
#endif
