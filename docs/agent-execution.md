# Bounded agent execution

Use the smallest check that answers the current question. A GPU-heavy game still
needs GPU evidence for shader, lighting, temporal and rendering changes. Avoid
paying for a full suite after a parser or logic edit. Profiles are explicit and
work on any machine; no hostname, driver name or assumed cloud capability selects
a profile. `hardware` is an authorization to run the full gate, not proof that a
physical GPU is present. CI explicitly opts in, retaining its previous coverage.

## Inspect, build, select

```sh
# In this saved cloud environment, if present:
source /workspace/.cloud-tools/activate.sh
python3 tools/agent.py doctor
make agent-list
make build BUILD_TARGET='test_suite chibi_ctl'
make agent-test AGENT_SELECTOR=--graph-optimize
make agent-gpu-test AGENT_SELECTOR=--model-sampled-gpu
```

`doctor` enumerates Vulkan devices without rendering. Device type 4 is CPU. On
2026-10-05 this environment exposed no `/dev/dri` or NVIDIA device, and both the
activated ICD and normal loader discovery reported only llvmpipe (LLVM 19.1.7).
Installed hardware ICD files alone do not establish accessible hardware.

The default `CHIBI_EXECUTION_PROFILE=iteration` allows reviewed CPU selectors and
bounded authoring previews. `test/execution_selectors.h` is the CPU allowlist.
Other selectors are GPU/mixed or unclassified, and are blocked with an outstanding
check rather than silently trimmed. For example, `--studio-documents` includes
window/render work; `--ui-bench` mixes headless and Studio cases. New selectors
must be inspected and classified before adding them to this list.

`gpu-check` opts into `--model-sampled-gpu` or `--shader-detail-gpu`, with a 60-second
native deadline. Their assertions, reference resolutions and frame sequences are
unchanged. Use this only when relevant to the edit. Other rendering selectors
remain available in the full profile. CPU checks use existing sanitizer settings;
the agent wrapper imposes a 180-second deadline without suppressing assertions or
leak checks. Timeouts are failures/blocked results, never passes.

## One output image and explicit MP4

Use existing MCP `previewScene` / `previewShader` or `chibi_ctl --render` requests.
For a scene, defaults are one output frame after one GPU warmup frame, 640x360,
and a 30-second deadline. Shader previews draw one frame at their existing 256x256
default. Set scene `gpu_warmup_frames` explicitly (0..32) when history needs it;
these frames count toward actual work and appear in the response. Initial CPU
simulation `frames` is limited to 60. The preview pixel limit is 230400, with
neither side above 640. Lower resolution is useful for iteration but can hide
fine detail and representative memory pressure. One output cannot prove history
convergence, motion or temporal stability.

Bounded previews and MP4 capture use the native controller's **hidden-window**
renderer, save their output, then exit. They do not launch the Studio editor.
Shader previews already use this mode. Hidden-window rendering is headless from
the user's perspective, but is not a simulation-only run or a guarantee of a
window-system-free backend: SDL/Vulkan may still require a display context, and
rendering still consumes graphics memory and GPU work (or software-renderer CPU).
The native response reports `window_hidden`.

When the backend needs a display and none is available, use the existing Xvfb
fallback around the bounded command, explicitly reporting the display context:

```sh
xvfb-run -a env CHIBI_PREVIEW_DISPLAY_CONTEXT=xvfb python3 tools/agent.py preview preview.json --project-root projects/your-game --output /tmp/preview-result.json
```

The same prefix works for `mp4`; the renderer's frame, resolution, RSS and time
budgets remain active. Xvfb supplies a virtual display, not a GPU or a free render.
If Xvfb/display initialization is unavailable, report the failure instead of
launching Studio or retrying an unbounded renderer. Explicit full-profile
performance runs retain their existing window mode; editor/UI tests are unchanged.

Example `preview.json`:

```json
{"operation":"preview_scene","kind":"scene","name":"YourScene","width":320,"height":180,"gpu_warmup_frames":2}
```

```sh
python3 tools/agent.py preview preview.json --project-root projects/your-game --output /tmp/preview-result.json
# Only when an MP4 is explicitly wanted (60 fps, 1 second here):
python3 tools/agent.py mp4 preview.json --project-root projects/your-game --frames 60 --output /tmp/preview.mp4
```

The preview file preserves the native JSON response and encoded PNG. The MP4
command reuses scene capture, verifies exact raw byte count, then runs ffmpeg.
It requires ffmpeg/libx264 and never falls back to a performance run. MP4 budgets:
3..180 output frames (up to 3 seconds at 60 fps), 0..32 warmup frames, 640x360 pixel
budget, at most 256 MiB raw capture bytes, 120 seconds rendering plus 30 seconds
encoding. Partial captures are temporary and are not published as success.
Native fixed-reference performance runs retain their original 240 warmup frames
and 16 excluded measurement frames under the full profile.

The Linux agent wrapper and MCP adapter enforce a 2048 MiB renderer RSS limit.
RSS is sampled, so this is a stop budget, not a reservation or an exact GPU-memory
cap. Native render entry points independently enforce request/frame/byte/time
budgets; direct `chibi_ctl` callers do not get the wrapper's RSS monitoring.
The wrapper refuses bounded rendering on hosts without Linux `/proc`; use MCP
with the native limits or explicitly opted-in hardware there. Sanitizer virtual
address reservations are not capped as though they were physical memory.

Do not reconstruct a GPU suite as repeated preview calls. Each preview should
answer a specific visual question; it does not satisfy a test selector. GPU MCP
smoke suites require full opt-in even though they consist of individual previews.
There is deliberately no cross-process scheduler or global frame quota.

## Full gate and truthful handoff

```sh
CHIBI_EXECUTION_PROFILE=hardware CHIBI_ALLOW_FULL_GPU=1 make test test-render
```

`make test` still means the complete tooling/native gate; `test-render` adds the
existing render coverage. Neither silently becomes a focused test. The same
opt-in applies to full/mixed native selectors invoked directly. Keep required
backend, strict-sanitizer and representative-load checks on suitable hardware.
A successful focused result reports `full_verification:false`, skipped full
checks, and the next full-gate command. A blocked budget or unknown selector exits
nonzero. Do not describe partial coverage, timeouts or blocked checks as a green
merge gate.

Smallest saved-cloud-start change: keep the existing dependency activation and
caches, and optionally export `CHIBI_EXECUTION_PROFILE=iteration` (already the
code default). Run `doctor` once when capabilities change. No new installation,
driver changes, security changes, network changes or unpublished setup scripts
are required. This change does not edit the saved cloud setup.
