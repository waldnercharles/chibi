# Upstream

Source: [awlevin/typesafe-computer-use](https://github.com/awlevin/typesafe-computer-use)

Pinned commit: `cc7b5066ae1a07b5e3182e8f87a9b5b6dfdcffc1`.

Imported on 2026-09-21 under the MIT license. `LICENSE` retains Aaron Levin's
copyright; `README.upstream.md` retains the original usage documentation.

This is a source adaptation, not a wrapper around Codex computer-use tools.
The upstream OCR, accessibility merging, screenshot annotation, coordinate
conversion, click/scroll/key handlers, and step loop execute locally.

Changes:

- `laya.py` implements the typed choice interface against `/v1/decide`, with
  compact desktop observations, groups of at most six options for generic target
  selection, validated responses, and request traces.
- `supervisor.py` and `session.py` replace automatic writer calls in the local
  entry point with persistent, single-use Codex handoffs.
- The runner uses Laya exclusively, selects targets only when needed, bounds
  actions, checks observations before input, and requires supervisor verification
  before reporting completion.
- The CLI exposes start/resume/status/permissions. Chibi MCP wraps that same CLI.
- `editor.py` adds an opt-in structured Chibi connection and bounded asset-creation
  workflow using the same persistent sessions. Laya classifies the complete
  nine-item asset menu in one request, Codex supplies a name, and the workflow
  verifies field readback and saving. This path uses Chibi's own input queue;
  the general macOS path retains the upstream executor.
- Native capture filters background-window OCR; foreground lookup uses live System Events state.
  AppleScript strings are escaped, screenshots clean up temporary files, and
  Unicode keyboard events use UTF-16 lengths with the upstream abort gesture.

The upstream hosted writer helpers remain for reference and regression coverage.
The local CLI and MCP use persistent supervised sessions; they never construct
an Anthropic or TypeSafe client and require neither service's credentials.
