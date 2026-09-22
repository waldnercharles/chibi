# Laya editor supervision

## Commands inside the editor

Press **Cmd+K** on macOS or **Ctrl+K**, or click **Commands** beside Quick Open.
Enter a goal and press Enter. Quick Open retains Cmd/Ctrl+P.

The palette now runs an observe → choose → execute → observe loop. Its action
catalog comes from the running editor, rather than a list of supported user
requests. ImGui item hooks expose standard widgets automatically. Graph node
titles, pins and the canvas publish named regions through the same interface.
The observation contains stable IDs, labels, roles, current values when known,
enabled/active state, popup scope and bounds. Custom drawing must register an
actionable region; this is tooling accessibility, not VoiceOver support.

Laya can select an observed control, type into a textbox, right-click, scroll,
drag between observed targets, or press Enter, Escape or Tab. The native socket
and browser palette share the same input executor. No OS Accessibility prompt,
screenshot interpretation or Codex call is needed for these editor actions.
The loop takes a fresh observation after each action. Stale/disabled/blocked
targets and concurrent input are rejected. Text input is checked by readback.
Sixteen actions or repeated unchanged observations pause the loop.

Reflected property editing and opening indexed assets remain structured tools
within this general action catalog. Property editing supports up to four colors,
numbers or booleans on one prototype, validated before one Undo transaction.
Successful edits retain the target/component for a follow-up request. Clear
context removes that target. For example:

```text
Change the boost emitter particle color to green, and the end color to purple
Now make the end color blue
```

Other useful evaluation goals are:

```text
Create a material named AgentMaterial
Open the View menu
Hide the Inspector
Open the BoostEmitter behavior
```

These are evaluation cases, not a guarantee of correct model choices. Review
shows the actual proposed step and Laya's entropy confidence. Apply step runs
that step; Recheck observes again after manual intervention. Actions shows the
executed steps. Escape or Close cancels. The panel remains usable over modal
editor dialogs. Missing text pauses for a user/supervisor reply; Laya does not
generate prose, graph programs or shader code.

A confidence floor of 0.8 pauses uncertain choices. **Confidence is not a
calibrated success probability.** Current live evaluation still finds wrong
navigation choices and premature completion, including in material creation.
A high-confidence wrong asset-type selection was also observed while evaluating
full UI context. The general interaction boundary is implemented; reliable
arbitrary-task autonomy is not established. Laya's completion decision is shown
as a request to review the result, never as verified task success.

Native builds launch the stdlib-only `tools/editor-command/resolve.py` using
`python3`, or `CHIBI_PYTHON` when set. Both native and browser commands use
`LAYA_URL`, default `http://127.0.0.1:8768/v1/decide`, and `LAYA_API_KEY` or
`LAYA_API_KEY_FILE`, default `.chibi/laya-api-key`. The editor built from this
checkout needs the worker at its build-time source path.

For the browser, build with `make web`, then serve with:

```sh
python3 tools/web_serve.py --build build-web --project projects/arena2d --port 8110
```

The local server proxies commands using its existing same-origin token. The Laya
key stays on the server. A static HTML server alone cannot execute commands.
Restart the preview server after rebuilding; it snapshots its served artifacts.

### Validation, September 21

The deterministic native UI checks exercise real ImGui input and asset readback:

```sh
CHIBI_EDITOR_AGENT_ONLY=1 ASAN_OPTIONS=detect_leaks=0 MallocNanoZone=0 \
  build/test_suite --arena-ui
CHIBI_EDITOR_COMMANDS_ONLY=1 ASAN_OPTIONS=detect_leaks=0 MallocNanoZone=0 \
  build/test_suite --arena-ui
```

They cover discovered widgets, layout/menu navigation, graph selection, review
interaction over a modal, textbox replacement, material creation, compound
property edits, atomic Undo, invalid-second-edit rejection and stale snapshots.
Python checks cover the resolver and the existing native supervisor handoff;
JavaScript checks cover browser transport and cancellation. These checks verify
the executor and protocol, not Laya's general decision quality.

The rebuilt browser preview also resolved the compound color request, applied
both values after review, and restored both with one Edit → Undo. No test edit
was saved. The general chooser rated that correct proposal at only 7% confidence
and took 3.02 seconds. It proposed View for “Open the View menu” (14%, 0.96 s),
but incorrectly proposed Edit for “Hide the Inspector” (6%, 1.08 s). Opening the
menu after review worked; these observations do not establish reliable planning.
The input tests also cover returning the pointer to its human position and
restoring textbox focus before a key action after supervisor review.

The opt-in live material evaluation currently exposes model failures:

```sh
CHIBI_EDITOR_COMMANDS_ONLY=1 CHIBI_COMMAND_LIVE=1 CHIBI_COMMAND_APPROVE=1 \
  CHIBI_COMMAND_GOAL='Create a material named AgentMaterial' \
  ASAN_OPTIONS=detect_leaks=0 MallocNanoZone=0 build/test_suite --arena-ui
```

It approves only the expected creation steps and asserts the resulting asset.
It must fail when Laya proposes an unrelated action or ends prematurely. Do not
count an executor test, a confidence score, or this evaluation merely reaching
the New menu as successful autonomous creation.

| Before | After | Why |
| --- | --- | --- |
| A short list of typed commands | Observed widget/region catalog and repeated action selection | Make new editor controls reachable without adding intent recipes |
| One result per request | Action history, review/recheck and text checkpoints | Make longer attempts inspectable and recoverable |
| Reviewer blocked by a modal | Supervisor window shares the active popup's input scope | Keep review and text replies accessible |
| Individual field edits | Validated compound transaction and retained target | Support combined edits and follow-up requests |
| External supervisor drives each input | Local asynchronous worker and shared editor input | Remove Codex latency from individual actions |

The direct source adaptation is in [tools/typesafe-computer-use](../../tools/typesafe-computer-use/README.md).
It runs upstream macOS OCR, accessibility, and native input locally, with Laya
at the existing Rancher endpoint. Codex supplies text and reviews checkpoints.

Chibi MCP exposes `startComputerTask`, `resumeComputerTask`, and `getComputerTask`.
They run the same local Python CLI documented in the linked README. Refresh an
existing MCP connection to expose the added tools; the CLI works immediately.

Prefer structured Chibi authoring tools when they express the operation.
`chooseEditorAction` remains the small-menu Laya selector for that path. The
new native worker performs computer-use actions itself.

The earlier Codex-executed lab demonstration was a separate bounded experiment;
it did not adapt the upstream executor. Laya confidence remains uncalibrated,
and the model's stop decision requires supervisor verification.

## Structured live editor connection

Launch the native editor with a local socket:

```sh
CHIBI_EDITOR_SOCKET=/tmp/chibi-laya.sock build/chibi \
  --project projects/laya_lab --scene scenes/LayaLab.json
```

The socket is optional and restricted to the current user (mode 0600). It is
separate from `chibi_ctl`: the controller remains the authoring/compiler API;
this connection observes and operates the **already running editor**. It feeds
the same ImGui input queue used by native UI tests. It neither injects OS input
nor needs macOS Accessibility permission. A web editor uses the same
observation/input code through its local HTTP command bridge, rather than
connecting to this native socket.

`src/editor/ui/ui_shell.h` exposes semantic controls through the existing stable
hit-test IDs. Explicitly named controls include File/Edit/View menus, New Asset choices,
the asset-name dialog, and search fields. Standard ImGui widgets are also captured
automatically, and custom graph targets register their regions. Controls carry label, role, value,
enabled/focused/active state, bounds and popup depth. This is structured tooling
accessibility, not a claim that all editor controls are exposed to VoiceOver.

The newline JSON transport supports `snapshot`, `click`, `right_click`, `type`,
`press`, `scroll`, and `drag`. Mutations
require the observed process ID, connection instance and revision. Disabled controls, controls behind
popups, stale observations, and typing outside the active textbox are rejected.
Typing replaces the field without pressing Enter. The adapter verifies readback
before creating an asset. There are no screen captures, OCR passes or model
requests between these deterministic input steps.

Start the bounded creation workflow through MCP:

```json
{
  "goal": "Create a material named LayaConfidentMaterial.",
  "target_app": "chibi",
  "editor_socket": "/tmp/chibi-laya.sock",
  "workflow": "create_asset",
  "act": true
}
```

Pass this to `startComputerTask`. Laya selects the asset type from the actual
New menu in one request. The workflow requires an explicit creation task; it
is not a general intent router. It pauses below entropy-confidence 0.8 (or a
higher supplied threshold). The default asset-name field is empty for every
asset type, so creation cannot proceed until Codex supplies text.

Resume with the returned `run_id`, `request_id`, and `reply: {"text": "..."}`.
A reply is consumed before input and bound to the editor process, project and
observed state. The workflow checks that the new artifact did not already
exist, verifies the name field, creates, saves, and checks the saved JSON name
and graph dirty state. The result is a **named default asset**, not a completed
custom graph or game. Existing generic desktop tasks retain upstream OCR/native
input and the broader supervisor checkpoints.

The same CLI path is available without refreshing MCP:

```sh
tools/typesafe-computer-use/.venv/bin/python -m typesafe_computer_use.cli start \
  'Create a material named LayaConfidentMaterial.' --act --target-app chibi \
  --editor-socket /tmp/chibi-laya.sock --workflow create_asset
```

## Measured policy quality, 2026-09-21

The evaluation holds menu labels constant and compares a generic next-click
question with an asset-type classification question. It uses 24 hand-authored
requests in two menu orders: 9 development, 9 held-out, and 6 challenge requests.
The challenge group contains paraphrases, unsupported asset types and ambiguity.
These are small local probes, not evidence of general editor autonomy or an
independently calibrated confidence score. No thresholds were lowered after
seeing the results. Normalized entropy is not a success probability.

| Held-out requests, both menu orders | Generic question | Structured question |
| --- | ---: | ---: |
| Correct type selections | 17/18 | 17/18 |
| Accepted at confidence >= 0.8 | 4/18 | 13/18 |
| Incorrect accepted choices | 0 | 0 |
| Median entropy confidence | 0.408 | 0.873 |
| Median localhost model request | 697 ms | 566 ms |

Across all answerable requests, correct choices increased from 34/40 to 38/40.
All 12 challenge evaluations paused below threshold. Development and held-out
requests together yielded 27 accepted structured decisions with no accepted
errors, versus 8 for the generic formulation. Reversed menus are correlated
repeats, not 48 independent user tasks.

The live material run selected Material at 0.9339 confidence, made six editor
input operations, required one Codex text reply, and saved
`projects/laya_lab/content/materials/LayaConfidentMaterial.json` without decision
overrides. Native validation returned zero errors/warnings; shader preview
compiled and rendered. That run's model request took 6.86 s, substantially above
the fixture median; its two worker invocations took 7.22 s and 1.56 s, excluding
the supervisor handoff. Do not substitute the benchmark median for end-to-end
latency. The improvement is reliable state and fewer decisions; cold/contended
service latency remains measurable work.

Fixtures, evaluator and a compact result record live in
`tools/typesafe-computer-use/evaluation/`. Open the New Asset menu, then run:

```sh
PYTHONPATH=tools/typesafe-computer-use tools/typesafe-computer-use/.venv/bin/python \
  tools/typesafe-computer-use/evaluation/evaluate.py --socket /tmp/chibi-laya.sock \
  --out .chibi/laya-accuracy/recheck.json
```

A second live run created `LayaConfidentPrototype` at 0.938 confidence, again
with one text handoff and no decision override. The two worker invocations
totaled 0.84 s (0.51 s in model-client setup and inference), excluding CLI
startup and the handoff. Native prototype validation returned no diagnostics.

| Before | After | Why |
| --- | --- | --- |
| Materials/shapes/libraries appeared immediately with generated names | Every New Asset choice opens the focused name field | Explicit text handoff and readback before creation |
| OCR guessed labels and click centers | Stable IDs, roles, values and popup-aware availability | Prevent actions against clipped, disabled or obscured controls |
| Generic policy chose Save even for saved state | Workflow verifies lifecycle facts and persisted output | Confidence cannot override an invalid precondition |

Validation: 169 Python tests, six Node provider tests, native Debug build, and
the focused sanitizer-enabled UI regression passed. Live checks rejected both
a disabled Create button and a stale revision, then verified exact field
readback. The wider UI suite was also attempted using the earlier test binary;
it hit a heap-use-after-free in `refresh_prototype_choices` after prototype
Undo. It is not reported as passing. This task's focused native test can be run
with `CHIBI_EDITOR_SEMANTICS_ONLY=1 ASAN_OPTIONS=detect_leaks=0
build/test_suite --arena-ui` after building.
