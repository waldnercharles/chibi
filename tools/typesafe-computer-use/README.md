# Local Laya computer use

A direct adaptation of [typesafe-computer-use](https://github.com/awlevin/typesafe-computer-use).
It runs its own macOS capture → OCR/accessibility → Laya decision → native input
loop. Codex supervises checkpoints and supplies field text. See [UPSTREAM.md](UPSTREAM.md)
for provenance and retained upstream code.

## Setup

From the c-blueprints repository root:

```sh
uv sync --project tools/typesafe-computer-use --python 3.12
tools/typesafe-computer-use/.venv/bin/clicker permissions
```

The hosting app needs Screen Recording and Accessibility permission in macOS
System Settings → Privacy & Security. `permissions` only reads permission state.
Focusing a running editor also uses System Events through macOS Automation.

The default Laya endpoint is `http://127.0.0.1:8768/v1/decide`. Authentication reads
`.chibi/laya-api-key` at the repository root. Override with `LAYA_URL`,
`LAYA_API_KEY_FILE`, or `LAYA_API_KEY`. The model can run in Rancher; this executor
runs on the Mac that owns the editor. No TypeSafe or Anthropic credentials are used.

## Start and supervise

Open the target editor, then start a task. Live runs focus that process before
capturing. Omit `--act` to capture and decide without input or activation.

```sh
tools/typesafe-computer-use/.venv/bin/clicker start \
  'Create a material named LayaComputerMaterial' \
  --target-app chibi --act --steps 12 --burst 3
```

Stdout is JSON; diagnostics go to stderr. The response includes `run_dir`,
`screenshot`, `history`, and a `pending` request. Review the screenshot before
responding. Resume through stdin, so free text is never treated as shell code:

```sh
tools/typesafe-computer-use/.venv/bin/clicker resume RUN_DIR REQUEST_ID <<'JSON'
{"text":"LayaComputerMaterial","confirm_canvas_field":true}
JSON
```

`confirm_canvas_field` is needed only when the application exposes no accessible
text field. After native typing without a matching accessibility readback, the
worker pauses for visual verification. It does not clear text on a model's guess.

| Pending reason | Supervisor reply |
| --- | --- |
| `text` | `{"text":"..."}`; add `confirm_canvas_field:true` for a visually confirmed canvas field |
| `review` | `{"approve":true}` or `{"decision":{"kind":"click_item","item":"19"}}` using an observed ID |
| `continue`, `verify_text` | Review the screenshot, then `{"retry":true}` |
| `stale_observation`, `app_changed` | Review the target screen, then `{"retry":true}`; changed state produces a fresh checkpoint first |
| `verify_done` | `{"answer":"What the evidence shows","achieved":true}` or `false`; use `retry`/an override to continue |

`clicker status RUN_DIR` reads a run without acting. Ctrl-C, MCP cancellation,
or the upstream mouse-to-top-left gesture interrupts execution. Runs return
after three decisions by default and allow at most twelve executed actions.
Replays via `--image` never execute input, including with `--act`.

## Codex / Chibi MCP

Reconnect Chibi MCP after installing the executor to refresh its tool catalog:

- `startComputerTask({goal, target_app, act:true})`
- `resumeComputerTask({run_id, request_id, reply})`
- `getComputerTask({run_id})`

The MCP launches this exact CLI in a bounded worker process. It returns control
at each handoff; it does not ask Codex to execute individual clicks. Cancellation
terminates the worker. A desktop lock prevents overlapping local runs; replies
are consumed before input and cannot be replayed after a crash.

For structured authoring, keep using Chibi's `build*`, validation, and preview
tools. `chooseEditorAction` remains the small-menu Laya selector for that path.
The native loop is available for editor operations that need visible controls.

## Evidence and limits

Each run saves raw/annotated captures, full observations, actual Laya requests
and responses, action history, and its pending checkpoint under `.chibi/computer-use`.
These local files include visible text and supplied field text; authentication
headers are excluded from traces.

Confidence is the service's uncalibrated score. With hierarchical selection, the
minimum score along the path gates execution; final probabilities describe only
the selected group. Large menus are harder for the checkpoint than short choices.
OCR cannot identify every unlabeled icon, and custom canvas text fields need
Codex's visual confirmation. The model's `done` choice is always reviewed.

```sh
tools/typesafe-computer-use/.venv/bin/pytest tools/typesafe-computer-use/tests -q
tools/typesafe-computer-use/.venv/bin/ruff check tools/typesafe-computer-use
```

## Structured Chibi workflow

For the native Chibi editor, prefer `--editor-socket /tmp/chibi-laya.sock
--workflow create_asset` on the existing `start` command. Launch the editor with
`CHIBI_EDITOR_SOCKET=/tmp/chibi-laya.sock`. The same flags are MCP fields named
`editor_socket` and `workflow`. This is an explicit asset-creation workflow,
not a general-purpose planner. Laya chooses the asset type once from the actual
menu; deterministic UI input handles navigation and saving, and Codex supplies
the name through the existing text checkpoint. The fixed confidence floor is
0.8. The editor validates process/revision, popup scope, enabled state and text
focus. No macOS permission, screen capture or OCR is needed on this path.

See [the measurements and contract](../../docs/research/laya-editor-supervision.md)
and [evaluation fixtures](evaluation/README.md). The generic desktop executor
remains available for other tasks. Structured semantic coverage currently
includes creation, document menus and search, not the entire editor.
