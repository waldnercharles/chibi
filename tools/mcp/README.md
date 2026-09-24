# Chibi MCP adapter

This package contains the model-facing stdio adapter and its shared public
tool catalog. The native `chibi_ctl` controller provides headless automation
and an explicit `--render` mode for GPU previews.

From a checked-out repository, run:

```sh
make mcp-setup
```

The generated Codex configuration points at `launch.sh`. The launcher locates
the repository from its own path and starts the adapter immediately. The
adapter builds `chibi_ctl` in the background when it is missing, and the first
tool call waits for that build. Diagnostics go to stderr; stdout is reserved
for MCP JSON-RPC.

See the repository's [MCP workflow](https://github.com/waldnercharles/c-blueprints/blob/main/docs/mcp.md)
for authoring and validation guidance.
