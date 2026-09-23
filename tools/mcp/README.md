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

`editorCommand` sends requests to a running editor through the Cmd+K command
runtime. It accepts a native socket or a selected browser session. Inspect the
editor first, then submit and poll a task until it is applied or needs input.
