# Bounded agent execution

Use the smallest check that answers the current question. A GPU-heavy game still
needs GPU evidence for shader, lighting, temporal and rendering changes. Avoid
paying for a full suite after a parser or logic edit. Profiles are explicit and
work on any machine; no hostname, driver name or assumed device capability selects
a profile. `hardware` permits underlying hardware operations but does not prove
a physical GPU is present. CI currently reports partial coverage.

## Inspect, build, select

Use the existing image viewer for graph wiring and rendered details. The former
zoom MCP utility is retired.

```sh
python3 tools/agent.py doctor
python3 tools/agent.py plan logic    # or render / temporal
make agent-list
make test-build
make agent-test AGENT_SELECTOR=--source-edit
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
hardware/software classification only guides preview resolution and reporting;
assertion thresholds stay unchanged. Temporal plans require
an explicit MP4 request before their suggested capture command is executed.

`doctor` enumerates Vulkan devices without rendering. Device type 4 is CPU; integrated/discrete types identify hardware candidates.
Virtual/other types remain unknown rather than being called hardware acceleration. On
2026-10-05 this environment exposed no `/dev/dri` or NVIDIA device, and both the
activated ICD and normal loader discovery reported only llvmpipe (LLVM 19.1.7).
Installed hardware ICD files alone do not establish accessible hardware.
Discovery is not a rendered-device guarantee: execution results separately report
the actual native backend and device name from renderer logs when available,
or explicitly leave the device unknown. They also include image dimensions,
planned frame counts, elapsed wall time, and time/RSS budgets. Native render results
carry the same execution metadata. Unexecuted checks remain explicitly pending.

Shared GPU assertion fixtures use native hidden windows on macOS and Windows.
On Linux they prefer SDL's offscreen driver while honoring explicit
`SDL_VIDEO_DRIVER` or legacy `SDL_VIDEODRIVER` settings.

The default `CHIBI_EXECUTION_PROFILE=iteration` allows CPU selectors and
bounded authoring previews. `test/execution_selectors.h` is the CPU allowlist.
Other selectors are GPU/mixed or unclassified, and are blocked with an outstanding
check rather than silently trimmed. New selectors must be inspected and
classified before adding them to this list.

The `gpu-check` profile routes the installed-player and Studio source-edit
selectors; each needs the installed products named in `tools/agent.py`. CPU checks
use existing sanitizer settings without suppressing assertions or leak checks.
Timeouts are failures/blocked results, never passes.

## Reviewed checks and truthful handoff

`make test` runs the fast C execution-policy unit checks. `make test-tools`
runs all retained C tooling and process integrations. Select native and render
checks for the affected runtime contracts, preserving hardware opt-in where required.

Choose publication checks through independent review of the affected contracts
and their interactions. Preserve each selected check's backend, sanitizer and
hardware requirements. Report the command, candidate identity, configuration,
result and unexecuted coverage. Local evidence supports landing when CI is
disabled unless actual server rules require it. Explicit holds and approvals
remain binding. See [publication validation](testing.md#batch-validation-cut62).

## Bounded artifact validation (2026-10-05)

At the isolated PR151 base `95d70d0d`, the ASan/UBSan build exercised the existing
`projects/arena2d` scene `HRCSinglePixel` through Xvfb and Vulkan llvmpipe. Request:

```json
{"operation":"preview_scene","kind":"scene","name":"HRCSinglePixel","width":96,"height":64,"gpu_warmup_frames":1}
```

With that JSON saved as `/tmp/chibi-agent-feature-validation/request.json`, the
commands below produced actual artifacts (use fresh output paths when repeating):

```sh
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
through `make play`. No driver or desktop configuration is changed here.


## Review corrections for PR171

The runner owns its detached process group until completion. A finally block
terminates any remaining group members and reaps the direct child after Ctrl-C,
monitoring exceptions, time/RSS limits, failure, or normal completion. Ctrl-C
returns a structured interrupted result and exit 130. SIGTERM also unwinds cleanup,
reports `terminated_by_SIGTERM`, and exits 143; a real CLI subprocess regression
checks termination of its detached child and descendant. Descendant-liveness tests
exercise a real SIGINT, a monitoring exception, and a parent that exits before
its child; no GPU is used for these regressions.

The retired MCP adapter's transport regressions do not describe the current
CLI. Native execution metadata distinguishes offscreen, hidden-window,
visible-window, and unreported window state. Hardware validation remains
separate from CPU policy evidence.

### Installed Studio acceptance routes

The `--studio-source-edit` GPU route accepts explicit absolute
`CHIBI_STUDIO_SOURCE`, `CHIBI_STUDIO_BUILD`, and
`CHIBI_STUDIO_ACCEPTANCE_EVIDENCE` paths. It verifies the compiled acceptance
binary, recorded source files, SDK configuration and the exact three-file,
210-byte fixture. It copies that fixture to disposable scratch, isolates
preferences and thumbnail cache there, and removes scratch after execution.
Four hidden 1920×1080 frames at fixed 1/60 dt must report exactly one accepted
edit, undo and redo. UI initialization or runtime-frame diagnostics reject the
run. The ordinary startup route remains separate.

The Studio interaction routes use the same provenance checks and disposable
fixture:

| Agent selector | Hidden frames | Accepted interaction |
| --- | ---: | --- |
| `--studio-scene-collider3d` | 26 | Scaled box, sphere and capsule handles, typed history, parallel/nonfinite and hidden-handle rejection, Escape/H cancellation; three actors |
| `--studio-scene-gizmo3d` | 42 | World/local move, snapping, rotation/scaling, history and cancellation; six prototypes/eight actors |
| `--studio-graph-pointer` | 19 | SDL node selection, wire navigation, Find keys and pointer selection, Escape focus, Home framing |
| `--studio-asset-pointer` | 20 | SDL filter typing, list/grid clicks with modifiers, exact visible identities and selection |
| `--studio-debugger-source-history` | 47 | SDL source breakpoint, play, invalid draft, undo/redo, retained paused cursor and value, step and stop |

The acceptance source manifest must enumerate every file under
`tests/acceptance`, with 8–32 files, no file or directory symlinks, at most 256 KiB per file,
and exact recorded sizes and hashes. C-owned tooling cases reject missing,
unrecorded, changed, linked and oversized files. The resource pin covers the
Studio source project at consumer base `d10222dc`; these routes require a Studio
build containing the matching new selectors.

Restore `projects/studio/project.kdl` from `CHIBI_STUDIO_SOURCE` into
`$CHIBI_STUDIO_BUILD/share/chibi/studio/project.kdl` before each invocation.
Studio migrates the build copy on startup, so a second invocation otherwise fails
the unchanged-resource preflight. The engine route removes its disposable user
project after every run. See Studio's `tests/acceptance/README.md` for the exact
interactions. These focused routes do not release the held acceptance suite,
establish web input coverage or assert a particular GPU backend.
