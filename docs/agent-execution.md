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
python3 tools/agent.py plan logic    # or render / temporal
make agent-list
make build BUILD_TARGET='test_suite chibi_ctl'
make agent-test AGENT_SELECTOR=--graph-optimize
make agent-gpu-test AGENT_SELECTOR=--model-sampled-gpu
```

The agent wrapper runs lightweight discovery in an isolated process with a
five-second deadline before test/preview/capture actions. A failed loader is
reported as inconclusive and does not prevent bounded work. This probe examines
Vulkan only; unavailable Vulkan discovery is not evidence that Metal, GLES or
another rendering path is unavailable. Plans retain bounded rendering commands
and require the actual runtime result to establish the backend/device.
`plan logic|render|temporal` chooses focused commands using the explicit purpose
and discovered capabilities. Logic plans avoid render initialization even when
no graphics device exists. Render plans keep native reference assertions intact;
hardware/software classification only guides preview resolution and reporting,
not assertion thresholds or authorization for full suites. Temporal plans require
an explicit MP4 request before their suggested capture command is executed.

`doctor` enumerates Vulkan devices without rendering. Device type 4 is CPU; integrated/discrete types identify hardware candidates.
Virtual/other types remain unknown rather than being called hardware acceleration. On
2026-10-05 this environment exposed no `/dev/dri` or NVIDIA device, and both the
activated ICD and normal loader discovery reported only llvmpipe (LLVM 19.1.7).
Installed hardware ICD files alone do not establish accessible hardware.
Discovery is not a rendered-device guarantee: execution results separately report
the actual native backend and device name from renderer logs when available,
or explicitly leave the device unknown. They also include image dimensions,
planned frame counts, elapsed wall time, and time/RSS budgets. MCP render results
carry the same execution metadata. Full checks remain explicitly pending.

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

On Linux, bounded previews and MP4 capture prefer SDL's existing **offscreen**
video backend when the caller has not selected a driver. The native controller
checks that the SDL build includes that backend before selecting it, saves the
output, then exits. This needs no monitor, desktop changes, or Xvfb. The underlying
preview window remains hidden; no new renderer or Studio process is involved.
Both shader and scene previews use this selection. Headless rendering is not
simulation-only: it still consumes graphics memory and GPU work (or software
renderer CPU). Explicitly selected windowed backends may need a display context.
The native response reports `window_hidden` and the actual SDL `video_driver`.
The wrapper/MCP also report the native graphics backend and selected device from
renderer logs, not an assumption based on discovery. Software rendering keeps
exactly the same conservative budgets; offscreen does not imply hardware GPU use.

Explicit `SDL_VIDEO_DRIVER` and legacy `SDL_VIDEODRIVER` choices, including an
explicit empty value, are preserved. Other platforms and explicit full-hardware
profiles keep platform/caller selection. `make run`, `make play`, `make dev`, and
editor/UI tests are unaffected: selection occurs only in `chibi_ctl --render`
(and `--render-request`) for bounded profiles. A compiled-in offscreen driver can
still fail Vulkan/device initialization; report that failure with the requested
driver and SDL detail. No automatic fallback or extra render is attempted.

If offscreen is unavailable for a particular SDL/backend build, the caller may
explicitly choose the existing bounded Xvfb fallback. `CHIBI_PREVIEW_DISPLAY_CONTEXT=xvfb`
selects SDL X11 only when no driver was explicitly configured and reports the
actual runtime device, which may be software rendering:

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

Activation and provisioning are owned by CUT-49/CUT-52. This change consumes the
existing environment and does not own setup scripts or installation state.

Smallest saved-cloud-start change: keep the existing dependency activation and
caches, and optionally export `CHIBI_EXECUTION_PROFILE=iteration` (already the
code default). Run `doctor` once when capabilities change. No new installation,
driver changes, security changes, network changes or unpublished setup scripts
are required. This change does not edit the saved cloud setup.

## Bounded artifact validation (2026-10-05)

At the isolated PR151 base `95d70d0d`, the ASan/UBSan build exercised the existing
`projects/arena2d` scene `HRCSinglePixel` through Xvfb and Vulkan llvmpipe. Request:

```json
{"operation":"preview_scene","kind":"scene","name":"HRCSinglePixel","width":96,"height":64,"gpu_warmup_frames":1}
```

With that JSON saved as `/tmp/chibi-agent-feature-validation/request.json`, the
commands below produced actual artifacts (use fresh output paths when repeating):

```sh
source /workspace/.cloud-tools/activate.sh
xvfb-run -a env CHIBI_PREVIEW_DISPLAY_CONTEXT=xvfb python3 tools/agent.py preview /tmp/chibi-agent-feature-validation/request.json --project-root projects/arena2d --output /tmp/chibi-agent-feature-validation/preview.json
xvfb-run -a env CHIBI_PREVIEW_DISPLAY_CONTEXT=xvfb python3 tools/agent.py mp4 /tmp/chibi-agent-feature-validation/request.json --project-root projects/arena2d --frames 60 --output /tmp/chibi-agent-feature-validation/capture.mp4
ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,width,height,pix_fmt,r_frame_rate,nb_read_frames,duration -of json /tmp/chibi-agent-feature-validation/capture.mp4
```

The first preview correctly failed at process exit: LeakSanitizer identified
1,048,768 leaked bytes in Cute's draw uniform arena. The existing sanitizer
cleanup patch now destroys that arena when destroying the draw context. No
sanitizer suppression or assertion change was used. After rebuilding:

- Preview: clean exit 0, 3.71 seconds including Xvfb/wrapper startup, one output
  plus one warmup frame, native `window_hidden:true` and `planned_gpu_frames:2`.
  Decoded PNG was 96x64 with 359 distinct colors and opaque alpha; inspected
  pixels showed the small source and faint receiver, rather than a blank image.
- MP4: clean exit 0, 6.05 seconds including rendering and encoding; 60 output
  frames plus one warmup frame. Exact raw capture size was checked before
  encoding. ffprobe and full decode confirmed H.264/yuv420p, 96x64, 60 fps,
  60 decoded frames, and 1.000 seconds. The decoded RGB stream was 1,105,920
  bytes, with eight distinct frames and 137 distinct colors in its first frame.
- Observed peak child RSS was 891,460 KiB for preview and 1,052,136 KiB for video,
  below the 2048 MiB stop budget. Render deadlines remained 30/120 seconds,
  with a separate 30-second encoder deadline. Raw video was 1,474,560 bytes,
  below the 256 MiB limit. No budgets were increased.

These are functional artifact-path checks on a CPU renderer, not hardware GPU
performance, temporal convergence, representative-resolution quality, or full
regression certification. Xvfb provided a virtual display; both bounded native
render paths used hidden windows and never launched Studio. The 11 CPU policy
tests and MCP failure/timeout test passed again after the cleanup correction.

## CUT-51 publication checkpoint

Reconciled onto PR151 candidate `be4d5c0d`, then stable main
`de73cb85652f43694957377dfc3607d2e15da386`; those base trees are identical. The
draw uniform cleanup is already supplied by main, so the policy change has no
dependency-patch diff. The original `a772e85c` branch and its artifact evidence
remain preserved separately.

The reconciled ASan/UBSan build passed 14 CPU policy checks, including typed
hardware/software discovery, unknown/virtual devices, bounded health-probe
failure, logic/render/temporal plans, warmup, budgets, and refusal paths. The MCP
subprocess regression checks success metadata and ensures process failures or
timeouts cannot become passes. MCP schema/package and workspace Make checks
also passed. Focused native graph-optimizer, sampled-model CPU, and runtime
execution selectors passed; no full GPU suite ran.

The `HRCSinglePixel` request above also passed at 96x64 on the reconciled tree:
preview used one output plus one warmup frame (3.765 seconds); a three-output
MP4 smoke capture used one warmup frame (3.912 seconds). The preview decoded to
359 colors. ffprobe confirmed H.264, 96x64, three frames at 60 fps and 0.05 seconds.
The capture result reported the actual Vulkan device as
`llvmpipe (LLVM 19.1.7, 256 bits)`, `window_hidden:true`, Xvfb context, four planned
GPU frames, and unchanged 120-second render / 30-second encoding / 2048 MiB RSS
budgets. Earlier one-second/60-frame validation remains recorded above.

This is publication evidence for focused functionality, not full hardware
certification. Representative hardware/backend gates remain pending; inherited
GLES and NVK LeakSanitizer limitations remain owned by CUT-54/CUT-55. Startup
provisioning/activation changes remain owned by CUT-49/CUT-52.


## Linux offscreen follow-up

A focused local check without DISPLAY/WAYLAND_DISPLAY used the automatic SDL
offscreen route for a 96x64 `HRCSinglePixel` preview: one output plus one warmup,
3.77 seconds, exit 0, Vulkan device `llvmpipe (LLVM 19.1.7, 256 bits)`. The output
reported `video_driver:offscreen`; no Xvfb or Studio process was launched. Driver
selection tests cover explicit modern/legacy choices, Xvfb opt-in, non-Linux and
full-hardware preservation. A deliberately unavailable explicit driver fails
with native SDL context rather than silently switching routes.

The parent has separately verified SDL offscreen on Fedora RTX 4080 SUPER/NVK
through `make play`; a bounded validation of this controller route on that
hardware remains pending. No driver or desktop configuration is changed here.


## Review corrections for PR171

The runner owns its detached process group until completion. A finally block
terminates any remaining group members and reaps the direct child after Ctrl-C,
monitoring exceptions, time/RSS limits, failure, or normal completion. Ctrl-C
returns a structured interrupted result and exit 130. Descendant-liveness tests
exercise a real SIGINT, a monitoring exception, and a parent that exits before
its child; no GPU is used for these regressions.

The MCP adapter handles stdin errors when the controller rejects before reading
a request. It waits for the bounded child close to preserve the native refusal,
settles once, and never accepts a success object after an input error. An 8 MiB
request regression verifies this early-exit path. Execution metadata distinguishes
offscreen, hidden-window, visible-window, and unreported window state rather than
assuming all non-offscreen results are hidden. Hardware validation and required
full/CI gates remain pending; these corrections required no hardware slot.
