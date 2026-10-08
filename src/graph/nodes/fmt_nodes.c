#include "graph/nodes/fmt_nodes.h"

#include "graph/model/native_definition.h"
#include "runtime/text_format.h"

static pin_spec fmt_text_inputs[] = { NATIVE_FORMAT_TEXT_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec fmt_text_outputs[] = { NATIVE_FORMAT_TEXT_OUTPUTS(GRAPH_NATIVE_PORT) };
node_desc NODE_Format_Text = {
    .name = NATIVE_FORMAT_TEXT_NAME,
    .source_name = NATIVE_FORMAT_TEXT_SOURCE,
    .input_count = sizeof fmt_text_inputs / sizeof *fmt_text_inputs,
    .output_count = sizeof fmt_text_outputs / sizeof *fmt_text_outputs,
    .inputs = fmt_text_inputs,
    .outputs = fmt_text_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_format_text,
    .execution_call = &NATIVE_FORMAT_TEXT_CALL,
    .host_call = &NATIVE_FORMAT_TEXT_HOST,
    .source_variadic_optional = NATIVE_FORMAT_TEXT_VARIADIC_OPTIONAL,
    .access = &NATIVE_TEXT_ACCESS,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Text",
    .keywords = "format string text printf substitute",
    .description = "Replaces {} or {0} placeholders in Format with the "
                   "arguments, fmt style; {0:.2f} adds a spec.",
};

static pin_spec text_equal_inputs[] = { NATIVE_TEXT_EQUAL_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec text_equal_outputs[] = { NATIVE_TEXT_EQUAL_OUTPUTS(GRAPH_NATIVE_PORT) };
node_desc NODE_Text_Equal = {
    .name = NATIVE_TEXT_EQUAL_NAME,
    .source_name = NATIVE_TEXT_EQUAL_SOURCE,
    .input_count = sizeof text_equal_inputs / sizeof *text_equal_inputs,
    .output_count = sizeof text_equal_outputs / sizeof *text_equal_outputs,
    .inputs = text_equal_inputs,
    .outputs = text_equal_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_text_equal,
    .execution_call = &NATIVE_TEXT_EQUAL_CALL,
    .host_call = &NATIVE_TEXT_EQUAL_HOST,
    .access = &NATIVE_TEXT_ACCESS,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Text",
    .keywords = "equal same compare string text empty match",
    .description = "Whether A and B are the same text, compared byte by byte; "
                   "compare with \"\" to test for empty text.",
};
