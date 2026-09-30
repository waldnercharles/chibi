#include "diagnostics/web_trace.h"

#ifdef CHIBI_ENABLE_WEB_TRACE
#include <emscripten.h>

/* Names are static, so each is decoded once. */
EM_JS(void, measure, (const char *name, double start), {
    const names = Module['chibiTraceNames'] || = new Map();
    let text = names.get(name);
    if (text == = undefined) {
        text = UTF8ToString(name);
        names.set(name, text);
    }
    performance.measure(text, { start, end: performance.now() });
})

web_trace_zone web_trace_begin(const char *name)
{
    return (web_trace_zone){ name, emscripten_get_now() };
}

void web_trace_end(web_trace_zone zone)
{
    measure(zone.name, zone.start);
}
#endif
