# Studio collaboration

Studio rooms stream editor actions over a WebSocket. GitHub Pages serves the
ordinary Emscripten application; the room service runs separately. The host owns
the editable model and validates proposals through the existing authoring APIs.
The service orders accepted actions and retains the latest checkpoint and its
action tail. Viewers replay that stream. An editor invite permits proposals.

## State and authority

The C session in `src/editor/collab/` belongs to the application. Its lifecycle is
offline, connecting, loading a checkpoint, ready, recovering, or closed. Inbound
messages are queued and drained before the application frame changes editor
state. A replay guard prevents accepted actions from publishing themselves.

An editor sends a proposal without applying it optimistically. The host rejects
a proposal whose base revision is stale, or applies it through normal document
validation and sends an acceptance. The service assigns the next revision and
broadcasts it. The host recognizes its already-applied acceptance; other clients
apply it once. Only one mutation may be outstanding locally. This makes conflict
handling explicit: rejected edits must be retried against the current state.
There is no character merge or whole-file last-write-wins path.

The host retains normal document history for validated proposals. Remote replay
does not add another user's edits to a viewer's local undo stack. The local
workspace and room workspace have separate lifetimes. Browser room files live
on a temporary MEMFS mount; IndexedDB remains the local project's cache.

Host transfer is not supported. A disconnected host has a short reconnect grace
period. If it does not return, the service explicitly closes the room. A service
restart also ends its in-memory rooms. Reconnecting clients restore the last
verified checkpoint and replay the committed action tail before editing again.
An edit that never reached the service is not a committed room edit.

## Wire protocol

Every message has exactly these fields:

```json
{"protocol":1,"room":"room-id","client":"client-id","seq":1,"revision":8,"kind":"proposal","payload":{"family":"graph"}}
```

The example shows the envelope shape; each action also needs the operation and
its typed arguments. Identities contain ASCII letters, digits, underscores, and
hyphens. Sequence numbers and revisions are nonnegative JavaScript-safe integers.
The service authenticates the socket and verifies the claimed actor identity.

| Family | Purpose |
| --- | --- |
| `hello`, `join`, `joined` | Authenticate, join, and report role, host, revision, and next action sequence |
| `leave`, `presence`, `error` | Membership, throttled ephemeral presentation, and failures |
| `snapshot_request`, `snapshot`, `snapshot_ack`, `resync_required` | Verify and recover a workspace |
| `proposal`, `accepted`, `rejected`, `ack` | Submit, validate, commit, reject, and acknowledge an edit |
| `host_changed`, `room_closed` | Report authority availability or explicit closure |

Control messages use sequence zero. Mutations use a contiguous per-client
sequence starting at one; `(client, seq)` is their idempotency key. Reusing a
sequence with a different payload is an error. A proposal or host acceptance
contains its base revision. The service's acceptance contains the assigned
revision. Clients do not advance their revision merely because a packet arrived.
A gap or failed replay freezes editing and requests a checkpoint.

Action families are `graph`, `scene`, `text`, and `command`. Graphs and nodes use
their serialized IDs. Scene actors have stable authoring identities separate
from runtime ECS handles, which may change on restore. Text edits identify the
document path and part and carry byte ranges against a base revision. Studio
commands use registry IDs, never translated display labels. Presentation packets
carry normalized area coordinates and document, camera, selection, and caret
state. Peer presentation is ephemeral; it is not stored in `studio_prefs`.

Presence is `{presence_seq, data}`. It may be throttled or dropped. It does not
advance the session revision or authorize document edits.

## Checkpoints

A checkpoint is captured at a committed revision boundary, initially and
periodically after accepted edits. It includes unsaved scene and graph documents,
tabs, UI text buffers, histories, presentation, and the project's file archive.
Its payload is:

```text
checkpoint_version: 1
workspace: JSON string containing version, project, scene, graph, presentation
files: [{path: project-relative path, data: canonical base64}]
sha256: lowercase hexadecimal digest
```

`scene`, `graph`, and `presentation` inside `workspace` are JSON strings. Files
are sorted by their UTF-8 path bytes. Hash the UTF-8 bytes of exactly
`JSON.stringify({checkpoint_version: 1, workspace, files})`, preserving that
field order and omitting the hash field. Both the service and browser verify the
digest. Absolute paths, parent traversal, duplicate paths, invalid base64, and
oversized archives are rejected.

The browser maps project paths into its temporary room workspace. C constructs
a disposable candidate model and validates its scene, graph, and Studio state
before replacing the current model. Only successful restoration acknowledges
the snapshot. Existing workspace checkpoints retain their own version contract;
the collaboration wrapper does not change the project format.

## Access and deployment

For local development, start the dependency-free Node service and build the
browser Studio with its public URL:

```sh
node tools/collab/server/server.mjs
make web-serve CHIBI_COLLAB_URL=http://127.0.0.1:8787 WEB_PORT=8099
```

Open `http://127.0.0.1:8099` in one browser session and choose **Host room**.
Copy a viewer or editor invite into a separate browser profile or private window.
An invite link includes its credential; the room ID alone does not grant access.
Host creation preserves unsaved documents while moving the room into temporary
storage. Use **Leave** to return to the original local workspace.

The service issues short-lived room credentials at runtime. Host credentials can
issue separate viewer and editor invites. Each invite identifies one participant;
create a separate invite for each person. Viewer credentials cannot submit edits,
and editor credentials cannot accept proposals or upload canonical checkpoints.

Invite credentials travel in the URL fragment and are moved to per-tab session
storage for workspace recovery. They are never bundled into HTML, JavaScript,
WebAssembly, or project files. A room link grants access to the full shared
project and its unsaved documents. Only share an invite with its intended peer.

Production must serve the room service through HTTPS/WSS and restrict browser
origins to the deployed application's origin. The room creation credential and
token-signing secret belong in the service environment. GitHub Pages receives
only the public `CHIBI_COLLAB_URL` repository variable, an HTTP(S) service URL.
The Pages deployment does not deploy or provision the room service.

Run a single service process behind a TLS reverse proxy that forwards WebSocket
upgrades and preserves the `Origin` header. Set these service environment values
through the deployment provider's secret/configuration system:

| Variable | Value |
| --- | --- |
| `NODE_ENV` | `production` |
| `COLLAB_HOST`, `COLLAB_PORT` | Private bind address and port, normally `127.0.0.1` and `8787` |
| `COLLAB_ADMIN_TOKEN` | Room creation credential, at least 32 bytes |
| `COLLAB_TOKEN_SECRET` | Independent token signing secret, at least 32 bytes |
| `COLLAB_ALLOWED_ORIGINS` | Comma-separated exact HTTPS origins, including the Pages origin |
| `COLLAB_PUBLIC_WS_URL` | Public WSS origin, for example `wss://rooms.example.com` |
| `COLLAB_HOST_GRACE_MS` | Host reconnect grace, default `30000` |

The process command is `node tools/collab/server/server.mjs`; `GET /healthz` is
the health probe. The browser's configured HTTP(S) service origin must route to
the same process. No specific hosting provider is required. Multiple workers
need sticky routing and shared state, which this in-memory implementation does
not provide. Restarting or replacing the process closes existing rooms.

The service bounds rooms, membership, message sizes, retained decisions, replay
log size, and inbound packet rate. A checkpoint archive allows up to 4,096 files,
2 MiB per file, and 8 MiB of decoded files. Large projects must raise the matching
service and client limits deliberately. Invite tokens expire after 15 minutes;
host credentials expire after one hour. Expired credentials require a new invite
for a new connection.

The native editor uses a no-network adapter and keeps the same authoring APIs.
Room controls explain that connecting requires the web editor.

## Verification

Run `make test`, `chibi build`, and `make web`. The focused C protocol/replay
selector is `build/test_suite --collab`. It checks malformed and truncated
messages, ordering, duplicate acceptance, rejection, sequence gaps, snapshot
failure and recovery, replay non-recursion, and host departure.

Use separate browser sessions for the host and invited peer. Exercise join,
follow/unfollow, graph changes, scene transforms and fields, UI typing, stale
proposals, pointer movement, connection loss, page reload, checkpoint recovery,
and explicit leave. Check the C session revision and canonical document content
as well as the rendered view. `Module.chibiCollab.inspect()` exposes the C state
and workspace checkpoint for these checks; it contains shared project content
and should not be logged to a public service.
