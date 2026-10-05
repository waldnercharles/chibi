#pragma once
#include "third_party/yyjson/yyjson.h"
/* NULL means allowed. A refusal is JSON, not a successful render result. */
const char *chibi_render_policy(yyjson_val *request);
void chibi_render_deadline(void);

/* Bounded authoring stays hidden; explicit hardware benchmarks keep their mode. */
bool chibi_preview_hidden(int render_frames);
