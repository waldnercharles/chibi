# Multiplayer action-streamed editor

## Summary

Build a web-first collaboration session for Studio. GitHub Pages continues to
serve the static Emscripten application, while a separate realtime room service
orders and relays editor actions. A host/editor session remains authoritative;
other clients reconstruct the same editor view by replaying semantic actions
and applying periodic workspace checkpoints. This is action streaming, not
pixel or video streaming.

The first complete product is a shared room with presence, remote cursors,
perspective following, read-only spectators, and optional editor participants.
It must preserve the existing C model, document histories, validation paths,
and browser persistence boundaries.

## Context

- `.github/workflows/deploy-web.yml` publishes only `chibi.html`, JavaScript,
  WebAssembly, and data files to Pages. Pages is the distribution layer; it is
  not the room server.
- `tools/web_storage.js` gives each browser its own IndexedDB-backed project
  workspace. This remains a local cache, not the shared source of truth.
- `app_scene_authoring` owns scene documents, tabs, cameras, selections, and
  scene history (`src/editor/workspace/scene_internal.h`).
- `editor` owns behavior graphs, graph selection, and graph undo history
  (`src/editor/workspace/editor.h` and `editor_internal.h`).
- `studio` owns local presentation state, areas, focus, layout, active tabs,
  graph views, and local input (`src/editor/studio/studio_internal.h`).
- UI documents are edited through `text_edit` and `ui_buffer`; the text is the
  source of truth (`src/support/text_edit.h`, `src/editor/documents/ui_buffer.h`).
- `scene_history`, graph history, and `app_scene_authoring_checkpoint` are
  local undo/reload mechanisms. A checkpoint may be used for resynchronization,
  but it is not the live collaboration protocol.
- `tools/web_workspace.js` is a useful local-development precedent for a JS/C
  bridge, but its loopback HTTP polling and file mirroring are not multiplayer.

## System impact

The new source of truth for a room is:

1. A server-assigned monotonically increasing session revision.
2. An ordered log of accepted semantic actions.
3. The latest canonical workspace checkpoint for joining and resyncing.

The host applies proposed edits through the existing C mutation helpers. Only
accepted actions are broadcast. A remote replay path applies those actions
without treating them as local SDL input and without emitting them again.
Remote presentation state is ephemeral and is never written to
`studio_prefs`; local preferences remain local.

The protocol must model failure and recovery: connecting, joining, snapshot
loading, action acknowledgement, sequence gaps, reconnect, rejected actions,
host departure, room close, and clean shutdown.

## Recommended approach

Use a reliable ordered WebSocket transport for action and snapshot messages.
Keep the transport behind a narrow C-facing collaboration interface, with a
web JavaScript adapter for the first deployment. Isolate the room implementation
under `tools/collab/` so the browser can remain on GitHub Pages. Provide a
small local room server for tests and development; keep a production adapter
configuration separate from the editor binary.

Use host authority for the first full implementation:

- The host owns the canonical mutable editor session.
- Viewer clients receive a project/workspace snapshot, then replay actions.
- Editor clients send proposals; the host validates and applies them through
  the normal editor/document APIs, then broadcasts the accepted action.
- A periodic checkpoint and explicit sequence acknowledgements recover clients
  whose replay diverges.
- If host transfer is implemented, the server selects a replacement editor and
  promotes its most recent verified checkpoint. Otherwise, close the room
  explicitly when the host leaves; do not silently invent a second authority.

Do not solve concurrent editing by broadcasting raw mouse coordinates alone,
serializing every SDL event, or replacing the project with whole-file last-write
wins. Use normalized presentation coordinates and semantic operations with
stable document, graph, node, entity, and tab identifiers.

## Protocol

Define a versioned envelope with at least:

```json
{
  "protocol": 1,
  "room": "room-id",
  "client": "client-id",
  "seq": 42,
  "revision": 108,
  "kind": "action",
  "payload": {}
}
```

Required message families:

- `hello`, `join`, `joined`, `leave`, `presence`, `error`
- `snapshot_request`, `snapshot`, `snapshot_ack`, `resync_required`
- `proposal`, `accepted`, `rejected`, `ack`
- `host_changed`, `room_closed`

Required action families:

- Presentation: active document/tab, active area, maximized area, scene camera,
  graph camera, text caret/scroll, selection, and pointer position normalized
  to the relevant area.
- Studio commands: command ID plus typed argument, using the existing command
  registry rather than display labels.
- Graph operations: create/rename/open graph, spawn node, set pin default,
  connect/disconnect, and other operations already represented by
  `editor_command` or existing graph mutation helpers.
- Scene operations: entity selection, transform/property edits, rename,
  prototype/system changes, and file/tab operations through their existing
  authoring APIs.
- UI text: path, part, base revision, and one or more `text_edit` ranges.
- Snapshot metadata: content hash, session revision, and checkpoint version.

Presence packets may be lossy and throttled. Mutating actions must be reliable,
ordered, acknowledged, and idempotent by `(client, seq)`.

## Changes

- `src/editor/collab/collab.h`, `collab.c`, and protocol helpers: own room
  identity, role, connection state, sequence/revision tracking, outbound
  proposals, inbound replay, presence, and resync requests. Expose explicit
  `collab_begin_replay`/`collab_end_replay` or an equivalent guard so remote
  actions cannot recursively publish themselves.
- `src/editor/workspace/application.c`: tick the collaboration transport in the
  same lifecycle as the editor, drain inbound messages before the frame mutates
  state, publish accepted actions after mutations, and create checkpoints only
  at defined revision boundaries.
- `src/editor/studio/studio_host.c`, `studio_shell.c`, and
  `studio_commands.c`: capture semantic local actions and presentation changes;
  add host/join/leave, follow/unfollow, role, and connection-status commands.
- `src/editor/studio/studio_internal.h` and `studio_sources.c`: add ephemeral
  peer records, follow target, remote cursor/selection state, and connection
  status. Render peers as overlays without changing the local `editor.selected`
  or local SDL input ownership.
- `src/editor/workspace/scene_internal.h` and scene document modules: expose
  stable IDs and replay-safe operations for scene selection, camera, transforms,
  fields, tab changes, rename, and save/reload boundaries.
- `src/editor/workspace/editor.h` and graph mutation code: serialize and replay
  typed graph commands with stable graph/node IDs; reject invalid or stale
  commands through the same validation path used locally.
- `src/editor/documents/ui_buffer.*` and `studio_code.c`: capture remote text
  edits as one buffer operation, preserve the existing undo model, and trigger
  validation/live publishing only after the replayed edit is accepted.
- `src/editor/workspace/scene_checkpoint.c` and `src/editor/workspace/editor.c`:
  add a versioned collaboration snapshot wrapper if the existing checkpoint
  payload is insufficient. Do not change reload checkpoint compatibility without
  the required project/version migration.
- `tools/collab/client.js`: implement the browser WebSocket adapter and bridge
  messages to the C-facing API. Keep credentials and room tokens out of the
  static artifact; use short-lived room credentials.
- `tools/collab/server/`: implement the local and production room service,
  sequence ordering, membership/presence, snapshot storage, reconnect replay,
  duplicate suppression, proposal forwarding, host authority, and rate limits.
- `tools/web_shell.html` and the Emscripten build wiring: add room URL/config
  injection and a minimal status/recovery surface without replacing the canvas
  input model.
- `.github/workflows/deploy-web.yml`: pass only public runtime configuration to
  the Pages artifact; deploy the room service through its own controlled job or
  documented command.
- `test/collab_protocol.c`, `test/collab_replay.c`, and web/server tests: cover
  envelope validation, ordering, duplicate delivery, rejection, sequence gaps,
  snapshot recovery, replay non-recursion, stable IDs, text edit composition,
  and host departure.
- `docs/collaboration.md`: document the protocol, room lifecycle, security
  model, local two-client test setup, production deployment, and known limits.

## Done looks like

- Two independent browser sessions can join the same room from a GitHub Pages
  build using a room link or code.
- The joining client receives a verified snapshot and reaches the same active
  document, tab, camera, graph view, selection, and layout state as the host.
- Moving the host pointer, changing areas, panning/zooming, selecting nodes or
  entities, changing tabs, and moving the text caret appears in the remote view
  with the peer's identity and color.
- Host graph, scene, prototype, system, UI-text, tab, and presentation actions
  are captured semantically, ordered, acknowledged, and replayed by viewers.
- An editor-role client can submit supported mutations; the host applies them
  through existing validation/history paths, broadcasts the accepted result,
  and rejects stale or invalid proposals without corrupting local state.
- Remote replay never re-emits actions, steals local keyboard/mouse ownership,
  changes local preferences, or mutates a viewer's local undo stack in a way that
  changes the viewer's local editing semantics.
- A dropped connection, duplicated packet, delayed packet, or sequence gap
  results in deterministic resync from a checkpoint and continued replay.
- Reloading a client reconnects to the room and restores the latest verified
  session. Closing a room or losing its authority produces a visible, recoverable
  status rather than silent divergence.
- The native build still compiles and runs with a no-network transport stub;
  `make test`, `make build`, `make web`, the room-service tests, and a manual
  two-browser web smoke all pass.
- No private service secret is present in HTML, JavaScript, WebAssembly, or
  repository history.

## Verification

- Unit-test every protocol and replay invariant with malformed, duplicated,
  reordered, stale, and truncated messages.
- Run focused C tests for each action family before broad validation.
- Run the local room server and two browser clients against `make web-serve`.
- Exercise: join, follow, unfollow, host edits, viewer edits, invalid edits,
  rapid pointer movement, text typing, graph drag, scene transform, disconnect,
  reconnect, snapshot resync, room close, and page reload.
- Compare canonical content hashes and selected presentation fields after each
  scenario; do not treat a screenshot alone as proof that the models agree.
- Run `make test`, `make build`, and `make web`, reporting focused failures and
  environment failures separately.

## Escalation rule

The implementing agent must keep working through ordinary code, test, and local
deployment problems. It may stop and request guidance only when one of these is
true:

1. Production deployment requires an account, secret, billing choice, domain,
   or provider decision that is not available from the repository.
2. Two reasonable conflict/authority policies remain and the choice changes
   whether another user's edit can overwrite or merge with an existing edit.
3. A required editor mutation has no replay-safe existing helper, and a bounded
   implementation attempt plus a snapshot-resync fallback both fail.
4. A protocol or project-version migration would make existing saved projects
   unreadable and no migration policy is specified.
5. Security or privacy requirements conflict with the requested room behavior.

When escalating, provide exactly:

```text
Blocked: <one concrete decision or missing capability>
Evidence: <files, test output, and current state>
Tried: <bounded approaches and their results>
Options: A) <option and consequence>; B) <option and consequence>
Decision needed: <the smallest user choice that unblocks the work>
```

Do not silently replace action streaming with video, reduce the feature to
cursor-only presence, bypass existing mutation/validation helpers, or leave the
room protocol undocumented.
