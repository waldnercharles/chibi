#include "graph/compiler/validation.h"
#include "support/array.h"

void compile_report_init(compile_report *out)
{
    out->items = NULL;
    out->error_count = 0;
    out->warning_count = 0;
    out->note_count = 0;
    out->effects = (compile_effects){ 0 };
}

void compile_report_free(compile_report *out)
{
    if (!out) {
        return;
    }
    afree(out->items);
    afree(out->effects.reads);
    afree(out->effects.writes);
    out->error_count = 0;
    out->warning_count = 0;
    out->note_count = 0;
    out->effects = (compile_effects){ 0 };
}

int compile_report_blocked(const compile_report *out)
{
    return out && out->error_count > 0;
}

const compile_diagnostic *compile_report_first_error(const compile_report *out)
{
    if (!out) {
        return NULL;
    }
    for (int i = 0; i < acount(out->items); i++) {
        if (out->items[i].severity == CMPL_ERROR) {
            return &out->items[i];
        }
    }
    return NULL;
}

const compile_effects *compile_report_effects(const compile_report *out)
{
    return out ? &out->effects : NULL;
}

const char *cmpl_rule_name(int rule)
{
    switch (rule) {
        case CMPL_RULE_UNCONNECTED_EXEC: return "CMPL-4";
        case CMPL_RULE_INCOMPATIBLE_DATA_EDGE: return "CMPL-5";
        case CMPL_RULE_DATA_CYCLE: return "CMPL-6";
        case CMPL_RULE_MISSING_REFERENCE: return "CMPL-7";
        case CMPL_RULE_DUPLICATE_NAME: return "CMPL-8";
        case CMPL_RULE_UNRESOLVED_WILDCARD: return "CMPL-12";
        case CMPL_RULE_UNUSED_VARIABLE: return "CMPL-15";
        case CMPL_RULE_FUNCTION_LATENT: return "CMPL-9";
        case CMPL_RULE_FUNCTION_SHAPE: return "CMPL-10";
        case CMPL_RULE_MACRO_RECURSION: return "CMPL-11";
        case CMPL_RULE_DISABLED: return "CMPL-18";
        case CMPL_RULE_OPERATOR: return "CMPL-19";
        case CMPL_RULE_UNKNOWN_NOMINAL_TYPE: return "CMPL-20";
        case CMPL_RULE_SYSTEM_HOST_WAIT: return "CMPL-21";
        case CMPL_RULE_FIELD_PROJECTION: return "CMPL-22";
        case CMPL_RULE_CALL_SIGNATURE: return "CMPL-23";
        case CMPL_RULE_INPUT_BINDING: return "CMPL-24";
        case CMPL_RULE_BACKEND: return "CMPL-25";
        case CMPL_RULE_EDITOR_NODE: return "CMPL-29";
        case CMPL_RULE_QUERY_FUSION: return "PERF-QUERY-FUSION";
        case CMPL_RULE_QUERY_SCAN: return "PERF-QUERY-SCAN";
        case CMPL_RULE_CONTAINER: return "PERF-CONTAINER";
        default: return "CMPL-?";
    }
}
const char *cmpl_severity_name(int severity)
{
    return severity == CMPL_ERROR   ? "error"
         : severity == CMPL_WARNING ? "warning"
                                    : "note";
}
