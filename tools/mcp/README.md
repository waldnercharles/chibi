# Chibi MCP adapter

This package contains the model-facing stdio adapter and its shared public
tool catalog. The native `chibi_ctl` controller provides headless automation
and an explicit `--render` mode for GPU previews.

From a checked-out repository, run:

```sh
make mcp-setup
```

The generated Codex configuration points at `launch.sh`. The launcher locates
the repository from its own path and starts the adapter immediately; the
adapter builds missing native controllers in the background and the first tool
call waits for that build. Diagnostics go to stderr; stdout is reserved for MCP
JSON-RPC.

`make build` publishes `chibi_ctl` to a cache in the git common directory,
keyed by the committed native sources (`controller-cache.sh`). A worktree
without its own build uses the cached controller for identical sources, so it
works without compiling. Uncommitted native edits skip the cache.

See the repository's [MCP workflow](https://github.com/waldnercharles/c-blueprints/blob/main/docs/mcp.md)
for authoring and validation guidance.
