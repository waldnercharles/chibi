#ifndef CHIBI_CODE_PROGRAM_H
#define CHIBI_CODE_PROGRAM_H
#include "assets/code_source.h"
/* Executable syntax owns native callable declarations and control-flow AST.
 * Every semantic value comes from source; identities/layout are optional. */
typedef struct code_program code_program;
bool code_program_detect(const char *text);
behavior_asset *code_program_parse(code_source *source);
bool code_program_validate(code_source *source);
bool code_program_reserved(const char *name);
behavior_asset *code_program_parse_declarations(code_source *source);
void code_program_free(code_program *program);
void code_program_match_previous(code_source *source, const code_source *previous);
bool code_program_location(
    const code_program *program,
    uint64_t graph,
    uint64_t node,
    uint64_t pin,
    int *start,
    int *end
);
void code_program_remap(code_program *program, const uint64_t *from, const uint64_t *to, int count);
void code_program_reorder_outputs(code_program *, node *, const int *indices, int count);
void code_program_refresh_locations(code_source *source);
#endif
