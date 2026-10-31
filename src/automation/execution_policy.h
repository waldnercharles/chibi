#pragma once
#include <yyjson.h>
/* NULL means allowed. A refusal is JSON, not a successful render result. */
const char *chibi_render_policy(yyjson_val *request);

/* Bounded authoring stays hidden; explicit hardware benchmarks keep their mode. */
bool chibi_preview_hidden(int render_frames);

/* NULL preserves the caller/platform choice; only bounded Linux renders opt in. */
const char *chibi_preview_video_driver(bool linux_platform);
