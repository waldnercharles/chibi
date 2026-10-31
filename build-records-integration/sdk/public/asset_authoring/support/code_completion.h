#ifndef CHIBI_CODE_COMPLETION_H
#define CHIBI_CODE_COMPLETION_H

/* Symbol kinds shared by language services, Studio and automation clients. */
typedef enum
{
    CODE_COMPLETE_UNKNOWN,
    CODE_COMPLETE_FIELD,
    CODE_COMPLETE_VALUE,
    CODE_COMPLETE_TYPE,
    CODE_COMPLETE_FUNCTION,
    CODE_COMPLETE_KEYWORD,
    CODE_COMPLETE_ASSET,
} code_completion_kind;

static inline const char *code_completion_kind_name(code_completion_kind kind)
{
    switch (kind) {
        case CODE_COMPLETE_FIELD: return "field";
        case CODE_COMPLETE_VALUE: return "value";
        case CODE_COMPLETE_TYPE: return "type";
        case CODE_COMPLETE_FUNCTION: return "function";
        case CODE_COMPLETE_KEYWORD: return "keyword";
        case CODE_COMPLETE_ASSET: return "asset";
        case CODE_COMPLETE_UNKNOWN: return "unknown";
    }
    return "unknown";
}

#endif
