# MCP server discoverability and distribution for Chibi

Researched September 9, 2026. External facts below come from the current MCP
specification, Official MCP Registry, MCP TypeScript SDK documentation, and
official OpenAI documentation.

## Recommendation

Build a small **local, stdio MCP server first**, as a TypeScript package that
uses a narrow Chibi command-line interface. Publish it to npm and the
Official MCP Registry once its read-only workflows are useful. Add a remote
Streamable HTTP deployment only when a remote ChatGPT app or shared
installation is an actual goal.

That sequence makes the project useful to Codex and other local MCP hosts
without first turning a native desktop editor into an internet-facing service.
It also preserves one automation contract for a later remote server.

## What "available and discoverable" means

| Goal | Meaning | Practical route |
| --- | --- | --- |
| Available to the developer's local agent | The host launches the server for a workspace and calls its tools. | Distribute a stdio package and configure the host with its command. MCP clients should support stdio; the client launches the server as a subprocess. [MCP transports](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports) |
| Discoverable by MCP tooling | Searchable metadata tells a host/user how to install or connect to the server. | Publish `server.json` to the [Official MCP Registry](https://registry.modelcontextprotocol.io/). The registry is a catalog of metadata, not a package host. [Registry quickstart](https://modelcontextprotocol.io/registry/quickstart) |
| Connectable by people who do not have this checkout | A hosted service has a stable HTTPS endpoint. | Offer Streamable HTTP at `/mcp`, with public reachability, authentication, and Registry `remotes` metadata. [Remote-server publishing](https://modelcontextprotocol.io/registry/remote-servers) |
| Discoverable inside a ChatGPT workspace | A workspace administrator has reviewed and published a custom MCP app. | Create a remote MCP app in ChatGPT developer mode, scan tools, then publish it to the workspace. This is workspace distribution, not an automatic public directory listing. [ChatGPT developer mode](https://help.openai.com/en/articles/12584461) |

Registry publication is the right public-MCP discovery baseline, but it does
not make every host auto-install the server. The Registry is still preview,
and its metadata versions are immutable; it currently does not support
unpublishing. Treat the package name, description, repository URL, and first
published version as release artifacts. [Registry FAQ](https://modelcontextprotocol.io/registry/faq)

## Distribution plan

### 1. Local package: the valuable first release

Keep the MCP implementation outside the C application (for example,
`tools/mcp/`). Publish a Node/TypeScript package that speaks stdio and invokes
a purpose-built `Chibi` automation CLI. Stdio is designed for a host to
launch as a child process; it is not an HTTP service and must write only MCP
messages on stdout. Log diagnostics to stderr. [MCP transports](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)

The automation CLI should use the current internal command/query boundary
instead of manipulating JSON files as text. `editor_command_apply` already
routes graph changes through the same mutation helpers, validation, undo
history, and persistence path used by the UI; `editor_query_state` exposes a
read-only state view ([`editor.h`](../../src/editor/workspace/editor.h)). The
app also already has headless, fixed-frame, scene, and screenshot arguments
([`application.c`](../../src/editor/workspace/application.c)). These are strong
foundations for reproducible agent workflows.

Start with a compact tool set:

| Tool | Safety | First implementation |
| --- | --- | --- |
| `project_status` | Read-only | Build/configuration status, open authored document, compile diagnostics, dirty state. |
| `list_assets` / `read_asset` | Read-only | Typed summaries and canonical JSON for a named scene, behavior, prototype, material, or library. |
| `validate_asset` | Read-only | Load/compile the named asset and return machine-readable diagnostics. |
| `run_scene_check` | Read-only | Invoke deterministic headless frames, optional screenshot, and report output paths. |
| `apply_edit` | Write, phase 2 | Apply one typed editor command or a small validated batch; return a semantic diff, diagnostics, and saved path. |

Do not begin with a generic `run_shell`, a free-form source-writing tool, or a
tool per UI click. Those expose a broad authority surface without giving an
agent the semantic guarantees that make this editor distinct.

### 2. Publish for public MCP discovery

When the local package is stable:

1. Publish the built package to npm.
2. Give the package a stable `mcpName`, for example
   `io.github.waldnercharles/c-blueprints`.
3. Generate `server.json` with `mcp-publisher init`, describe the stdio package
   and its required environment/configuration, and publish with
   `mcp-publisher publish`.
4. Verify discovery with the Registry `GET /v0.1/servers?search=...` endpoint.

The Registry quickstart requires an npm package's `mcpName` to agree with
`server.json`; GitHub-authenticated names use the `io.github.<account>/...`
namespace. It also documents the official publisher CLI and the verification
query. [Registry quickstart](https://modelcontextprotocol.io/registry/quickstart)

The same `server.json` can later advertise both the npm stdio package and a
remote endpoint. That lets hosts choose the installation method. [Remote-server
publishing](https://modelcontextprotocol.io/registry/remote-servers)

### 3. Remote server: only for sharing or ChatGPT

Use current Streamable HTTP, not the retired standalone HTTP+SSE design. The
server provides one endpoint that accepts POST and GET; SSE is optional for
progress and notifications. [MCP transports](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)

The Registry requires a remote endpoint declared in `server.json` to be
publicly accessible. It recommends `streamable-http`, while allowing an SSE
entry for older clients. [Remote-server publishing](https://modelcontextprotocol.io/registry/remote-servers)

For ChatGPT specifically, a local server cannot be connected directly. ChatGPT
uses remote MCP servers; private-network and developer-machine deployments can
use Secure MCP Tunnel where it is available. Current full write/modify MCP
support is a beta rollout for Business, Enterprise, and Edu workspaces;
custom-server changes are frozen until an administrator refreshes and approves
them. [ChatGPT developer mode](https://help.openai.com/en/articles/12584461)

## Auth and transport constraints

If a local Streamable HTTP endpoint is added, bind it to `127.0.0.1` and
validate `Origin`. Keep the first release stdio-only until remote use is
needed. MCP requires Origin validation for Streamable HTTP, recommends a
localhost-only bind for local servers, and recommends authentication for all
connections because otherwise DNS rebinding can reach local tools. [MCP transports](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)

For a public remote server, use HTTPS and OAuth instead of a long-lived shared
API key. A protected MCP server is an OAuth resource server: it validates
tokens, publishes protected-resource metadata, and tells a client where to
find authorization-server metadata. The spec requires token audience
validation and forbids forwarding the MCP token to upstream services.
[MCP authorization](https://modelcontextprotocol.io/specification/2025-11-25/basic/authorization)

The current TypeScript SDK has a supported Streamable HTTP handler and an
authorization gate. Its handler deliberately does not validate Host, Origin,
or bearer tokens itself; mount those protections ahead of it. The framework
adapters enable host/origin checks for local binds. [SDK HTTP serving guide](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/docs/serving/http.md)

If using OAuth with ChatGPT, ensure the authorization provider issues refresh
tokens (`offline_access` for OIDC where appropriate). Otherwise a user may have
to reauthenticate after the initial token expires. [ChatGPT developer mode](https://help.openai.com/en/articles/12584461)

## The project changes that create real MCP value

An MCP server is valuable here when it lets an agent make and verify a visual
programming change with the editor's own invariants, rather than merely read
files. Prioritize these project changes:

1. **Public automation API.** Promote a small, versioned C API/CLI around
   `editor_command_apply`, `editor_query_state`, authored document loading,
   validation, and save. Make every response structured JSON with stable IDs
   and actionable diagnostics.
2. **Transactional edits.** Support `preview` (semantic diff + diagnostics),
   `apply`, and `rollback`/backup. A mutation should either create one valid
   editor-history step or leave the asset unchanged.
3. **Deterministic verification.** Accept scene path, fixed time step, frame
   count, seed, and screenshot target. Return an artifact manifest and exit
   status. This turns an agent's claim into a reviewable check.
4. **Semantic resources.** Export the node catalog, pin types, component
   schemas, asset dependency graph, and current compile errors as MCP
   resources. This gives the model the vocabulary it needs before it proposes
   a graph edit.
5. **Optional live attachment.** After the CLI works, add a loopback IPC queue
   consumed on the editor's main thread. It can expose the same commands to a
   running editor without crossing UI thread ownership. Keep it opt-in and
   issue a per-launch capability token.

## An ambitious direction worth pursuing

Build an **agent-safe visual-programming change loop**:

`inspect semantic model -> propose typed edit batch -> editor validates and
shows diff -> compile -> deterministic scene run -> screenshot/trace -> retain
or roll back`

This is more distinctive than a generic MCP wrapper. It uses the editor's
asset model, compiler diagnostics, runtime controls, and screenshot path to
make visual behavior work reviewable. A later live mode could show an in-editor
"agent change set" panel that previews the semantic diff, generated nodes and
wires, diagnostics, and before/after run artifacts before committing.

## Release gates

- The stdio server protocol test confirms stdout contains only MCP frames.
- Every read tool returns stable JSON and never mutates workspace state.
- Every write tool validates before saving, gives a clear diff/result, and has
  a failed-edit test that proves no partial asset change survives.
- A headless fixture runs deterministically and produces a screenshot/trace
  artifact the user can inspect.
- The npm tarball installs in an empty directory and the Registry record
  resolves to that exact version.
- Before any remote release: HTTPS, Origin/Host checks, OAuth metadata,
  audience validation, per-tool scopes, rate limits, audit logs, and an
  external security review are complete.

## Sources

- [MCP transport specification, 2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)
- [MCP authorization specification, 2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25/basic/authorization)
- [Official MCP Registry publishing quickstart](https://modelcontextprotocol.io/registry/quickstart)
- [Official MCP Registry remote-server publishing](https://modelcontextprotocol.io/registry/remote-servers)
- [Official MCP Registry FAQ](https://modelcontextprotocol.io/registry/faq)
- [MCP TypeScript SDK HTTP serving guide](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/docs/serving/http.md)
- [OpenAI: Developer mode and MCP apps in ChatGPT](https://help.openai.com/en/articles/12584461)
