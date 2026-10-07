#include "assets/code_program_internal.h"
#include "assets/code_source_internal.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool code_program_reserved(const char *name)
{
    static const char *const words[] = {
        "behavior", "library", "system",   "state",  "on",       "if",
        "else",     "while",   "for",      "in",     "break",    "continue",
        "return",   "result",  "value",    "action", "event",    "function",
        "macro",    "self",    "entry",    "true",   "false",    "null",
        "int",      "float",   "bool",     "string", "entity",   "set",
        "map",      "owner",   "domain",   "query",  "disabled", "engine",
        "foreach",  "await",   "sequence", "branch", "events",   "pure",
        "exit",     "local",   "goto",     "label",  "match",    "case",
        "default",  "cast",    "fallback", "after",  "shared",   "bind",
        "without",  "nil"
    };
    for (size_t i = 0; i < sizeof words / sizeof *words; i++) {
        if (!strcmp(name, words[i])) {
            return true;
        }
    }
    return false;
}
static bool state_name(const code_program *program, const char *name)
{
    for (int i = 0; i < acount(program->states); i++) {
        if (!strcmp(program->states[i].name, name)) {
            return true;
        }
    }
    return false;
}
static void name_error(code_source *source, const char *name, int start, int end)
{
    bool quoted = start >= 0 && end > start && source->text[start] == '"';
    if (name && !quoted && code_program_reserved(name)) {
        code_source_error(source, start, end, "Declaration uses a reserved language name");
    }
}
static void block_names(
    code_source *source,
    const code_program *program,
    const continuation *handler,
    statement *body,
    const char ***loops
)
{
    for (int i = 0; i < acount(body); i++) {
        statement *statement = &body[i];
        bool loop = statement->kind == CODE_STMT_FOR ||
                    statement->kind == CODE_STMT_FOREACH;
        if (loop && statement->alias) {
            name_error(
                source,
                statement->alias,
                statement->target_start,
                statement->target_end
            );
            if (state_name(program, statement->alias)) {
                code_source_error(source, statement->target_start, statement->target_end, "Loop binding shadows a state variable");
            }
            for (int j = 0; j < acount(handler->aliases); j++) {
                if (!strcmp(handler->aliases[j]->name, statement->alias)) {
                    code_source_error(source, statement->target_start, statement->target_end, "Loop binding shadows a component alias");
                }
            }
            for (int j = 0; j < acount(*loops); j++) {
                if (!strcmp((*loops)[j], statement->alias)) {
                    code_source_error(
                        source,
                        statement->target_start,
                        statement->target_end,
                        "Loop binding shadows an enclosing loop binding"
                    );
                }
            }
            apush(*loops, statement->alias);
        }
        block_names(source, program, handler, statement->body, loops);
        if (loop && statement->alias) {
            asetlen(*loops, acount(*loops) - 1);
        }
        block_names(source, program, handler, statement->alternate, loops);
    }
}
static void string_bytes(code_source *source)
{
    code_lexer lexer;
    code_lex_init(&lexer, source->text);
    code_token token;
    while ((token = code_lex_next(&lexer)).kind != CODE_TOKEN_END) {
        if (token.kind == CODE_TOKEN_ERROR) {
            break;
        }
        if (token.kind != CODE_TOKEN_STRING) {
            continue;
        }
        size_t length = token.end - token.start;
        char *literal = xmalloc(length + 1);
        memcpy(literal, source->text + token.start, length);
        literal[length] = 0;
        av_doc *document = av_json_read(literal);
        const av_value *value = document ? av_doc_root(document) : NULL;
        if (av_is_str(value) && memchr(av_get_str(value), 0, av_get_len(value))) {
            code_source_error(source, (int)token.start, (int)token.end, "String literal contains embedded NUL byte");
        }
        av_doc_free(document);
        free(literal);
    }
}
bool code_program_validate(code_source *source)
{
    code_program *program = source->program;
    int before = acount(source->diagnostics);
    for (int i = 0; i < acount(program->states); i++) {
        name_error(
            source,
            program->states[i].name,
            program->states[i].start,
            program->states[i].end
        );
    }
    for (int i = 0; i < acount(program->scopes); i++) {
        scope *scope = program->scopes[i];
        for (int j = 0; j < acount(scope->declarations); j++) {
            declaration *declaration = scope->declarations[j];
            if (declaration->start < declaration->end) {
                name_error(
                    source,
                    declaration->name,
                    declaration->start,
                    declaration->end
                );
            }
        }
        for (int j = 0; j < acount(scope->components); j++) {
            name_error(
                source,
                scope->components[j].name,
                scope->components[j].start,
                scope->components[j].end
            );
            if (state_name(program, scope->components[j].name)) {
                code_source_error(
                    source,
                    scope->components[j].start,
                    scope->components[j].end,
                    "System parameter shadows a state variable"
                );
            }
        }
        for (int j = 0; j < acount(scope->locals); j++) {
            name_error(
                source,
                scope->locals[j].name,
                scope->locals[j].start,
                scope->locals[j].end
            );
        }
        for (int j = 0; j < acount(scope->continuations); j++) {
            continuation *handler = &scope->continuations[j];
            for (int k = 0; k < acount(handler->aliases); k++) {
                declaration *alias = handler->aliases[k];
                declaration
                    *handled = handler->source
                                 ? code_program_declaration(scope, handler->source)
                                 : NULL;
                bool implicit_event = !strcmp(alias->name, "event") && handled &&
                                      alias->symbol == handled->symbol;
                if (!implicit_event) {
                    name_error(source, alias->name, alias->start, alias->end);
                }
                if (!implicit_event && state_name(program, alias->name)) {
                    code_source_error(source, alias->start, alias->end, "Component alias shadows a state variable");
                }
            }
            const char **loops = NULL;
            block_names(source, program, handler, handler->body, &loops);
            afree(loops);
        }
    }
    string_bytes(source);
    return before == acount(source->diagnostics);
}
