// Paste into the console of a CHIBI_WEB_PROFILING build, then run
// await sampleStudio(10000) with the desired editor view settled.
// Zone durations measure CPU work and submission, not GPU execution time.
async function sampleStudio(durationMs = 10000) {
    const lastFrame = Module.chibiFrames().at(-1)?.time_ms ?? -Infinity;
    const before = Module.chibiEditorCounters();
    performance.clearMeasures();
    const start = performance.now();
    await new Promise(resolve => setTimeout(resolve, durationMs));
    const elapsed = performance.now() - start;
    const after = Module.chibiEditorCounters();
    const timings = new Map();
    for (const entry of performance.getEntriesByType('measure')) {
        if (entry.startTime < start) continue;
        if (!timings.has(entry.name)) timings.set(entry.name, []);
        timings.get(entry.name).push(entry.duration);
    }
    const measures = {};
    for (const [name, durations] of timings) {
        durations.sort((a, b) => a - b);
        const rank = p => durations[Math.ceil(p * durations.length) - 1];
        measures[name] = {
            count: durations.length,
            p50_ms: rank(0.5),
            p95_ms: rank(0.95),
            p99_ms: rank(0.99),
            max_ms: durations.at(-1),
        };
    }
    const subtract = (a, b) => Object.fromEntries(Object.entries(b).map(([key, value]) => [
        key, typeof value === 'number' ? value - a[key] : subtract(a[key], value),
    ]));
    const frames = Module.chibiFrames().filter(frame => frame.time_ms > lastFrame);
    performance.clearMeasures();
    return {
        entry: location.pathname + location.search,
        user_agent: navigator.userAgent,
        elapsed_ms: elapsed,
        viewport: [innerWidth, innerHeight],
        device_pixel_ratio: devicePixelRatio,
        cross_origin_isolated: crossOriginIsolated,
        frame_summary: Module.chibiFrameSummary(frames),
        counters_delta: subtract(before, after),
        counters_after: after,
        measures,
    };
}
