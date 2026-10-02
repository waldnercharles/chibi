# Rebuilt editor measurements

Recorded on 2026-10-02 after the initial UI fixes, before the final alignment
and float-serialization follow-up. Builds and native tests had stopped during
recording. Each sample lasted ten seconds. Chromium 154 used ANGLE's
Metal renderer on an Apple M4 Pro with 48 GiB RAM and macOS 26.7. The viewport
was 1440×1000, device pixel ratio 1, with the worker build and cross-origin
isolation enabled. [Environment and source fingerprint](browser-environment.json).

| View / activity | Logged frames | Frame interval p95 | Editor zone p95 | Application frame p95 | Host emission p95 | Host emissions |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| [Movement graph, idle](browser-graph-idle.json) | 37 | 268 ms¹ | 1.6 ms | 2.5 ms | 0.6 ms | 37 |
| [Movement graph, continuous middle-button pan](browser-graph-pan.json) | 596 | 18 ms | 1.6 ms | 3.0 ms | 0.5 ms | 84 |
| [PauseMenu Canvas and source, idle](browser-ui-idle.json) | 600 | 18 ms | 1.6 ms | 2.3 ms | 0.9 ms | 40 |
| [Sandbox playing, Scene maximized](browser-sandbox-play.json) | 596 | 19 ms | 9.5 ms | 17.6 ms | 1.2 ms | 187 |

¹ The idle editor deliberately skips application frames for up to 250 ms while
checking for queued input on browser animation frames. Idle intervals describe
that scheduling policy, not interaction latency. The first interval in the pan
sample also includes the preceding idle period.

The idle graph and UI preview compiled no new host or graph geometry. Panning
reused intrinsic node measurements, route preparation, and reachability, while
the renderer compiled geometry for the changing view. The host emitted 84 times
during 596 pan frames; graph updates continued each frame.

Sandbox playback includes scene rendering and runtime work in the editor zone.
Its slowest logged frame was **68 ms**, with seven catch-up frames and no dropped
simulation time. That outlier remains in the raw data. These small-project
samples establish current behavior; they do not establish large-project
scaling or a whole-editor before/after speedup.

Zone durations measure elapsed CPU work and submission, including waits where
present. They are not GPU execution times, browser INP, or direct input-to-pixel
measurements. Percentiles use nearest rank. Raw JSON includes all zone counts,
p50/p95/p99/max values, frame summaries, and retained-render counters.

## Reproduction

Build with `make web WEB_CMAKE_FLAGS=-DCHIBI_WEB_PROFILING=ON`. The recording
served an isolated workspace copy with:

```sh
python3 tools/web_serve.py --build build-web \
  --workspace /tmp/chibi-ui-qa/workspace \
  --project /tmp/chibi-ui-qa/workspace/projects/arena2d --port 8137
```

Open `http://127.0.0.1:8137/chibi.html`, which selects the worker bundle. Paste
[browser-sample.js](browser-sample.js) into the console. After the view settles,
run `await sampleStudio(10000)` and save the returned object. For the pan case,
start `window.sample = sampleStudio(10000)`, pan across empty graph space for
the interval, then read `await window.sample`.

The graph was framed and maximized. The Canvas sample used Horizontal Split,
PauseMenu, Fit preview, and a selected MenuButton with its source visible.
Playback started from Sandbox, maximized Scene, and settled for 2.5 seconds
before sampling. Screenshots were captured outside the timed windows.

The sampler clears profiling measures before and after each recording. Avoid
leaving unbounded profiling traces running while performing other work.
