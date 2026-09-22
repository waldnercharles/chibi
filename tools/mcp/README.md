# Chibi MCP adapter

This package contains the model-facing stdio adapter and its shared public
tool catalog. The native `chibi_ctl` controller provides headless automation
and an explicit `--render` mode for GPU previews.

From a checked-out repository, run:

```sh
make mcp-setup
```

The generated Codex configuration points at `launch.sh`. The launcher locates
the repository from its own path, builds the native controllers when needed,
writes diagnostics to stderr, and reserves stdout for MCP JSON-RPC.

See the repository's [MCP workflow](https://github.com/waldnercharles/c-blueprints/blob/main/docs/mcp.md)
for authoring and validation guidance.

For Laya supervision, see [Laya editor supervision](../../docs/research/laya-editor-supervision.md).
`editorCommand` runs requests directly in the live editor through the same API
as Cmd+K. It supports native sockets and explicitly selected browser sessions;
see [Editor commands](../editor-command/README.md) for the contract and examples.
Refresh an existing MCP connection to load the new tool.
`chooseEditorAction` selects from structured actions; Codex executes the selected
Chibi operation. The direct [typesafe-computer-use adaptation](../typesafe-computer-use/README.md)
also runs its own native macOS OCR/accessibility/input loop. `startComputerTask`,
`resumeComputerTask`, and `getComputerTask` supervise that worker and supply text.
Install its Python environment before use and refresh an existing MCP connection.
Run the choice adapter tests with `npm run test:laya`; the Python package has its
own upstream and session regression suite.
