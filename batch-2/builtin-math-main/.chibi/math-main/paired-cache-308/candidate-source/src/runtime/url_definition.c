#include "runtime/url_definition.h"

#include "runtime/url.h"

static const native_parameter_definition OPEN_IN[] = { URL_OPEN_INPUTS(NATIVE_CSTRING_PORT) };
static const native_parameter_definition OPEN_OUT[] = { URL_OPEN_OUTPUTS(NATIVE_PORT) };
static const native_parameter_definition EXEC[] = { URL_EXEC(NATIVE_PORT) };
static const native_parameter_definition THEN[] = { URL_THEN(NATIVE_PORT) };

static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = URL_OPEN_NAME,
      .source_name = "open_url",
      URL_OPEN_PRESENTATION,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = OPEN_IN,
      .input_count = NATIVE_COUNT(OPEN_IN),
      .outputs = OPEN_OUT,
      .output_count = NATIVE_COUNT(OPEN_OUT),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = URL_CANCEL_NAME,
      .source_name = "cancel_open_url",
      URL_CANCEL_PRESENTATION,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = EXEC,
      .input_count = NATIVE_COUNT(EXEC),
      .outputs = THEN,
      .output_count = NATIVE_COUNT(THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = URL_OPEN_NAME,
      .implementation = { .call = &URL_OPEN_CALL, .access = &URL_ACCESS } },
    { .canonical_name = URL_CANCEL_NAME,
      .implementation = { .call = &URL_CANCEL_CALL, .access = &URL_ACCESS } },
};
const native_definition_pack URL_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
