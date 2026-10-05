#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd -P)
# Direct MCP launches share Make's opt-in detection and recursion guard.
if [ -f "$repo_root/tools/cloud/entry.sh" ] &&
   [ "$(bash "$repo_root/tools/cloud/entry.sh" --probe)" = 1 ]; then
    exec bash "$repo_root/tools/cloud/entry.sh" sh "$0" "$@"
fi
default_project_root="$repo_root/projects/arena2d"
project_root=${CHIBI_ROOT:-}
if [ -z "$project_root" ]; then
    if [ -f "$default_project_root/project.kdl" ]; then
        project_root="$default_project_root"
    else
        project_root="$repo_root"
    fi
fi
case "$project_root" in
    /*) ;;
    *) project_root="$repo_root/$project_root" ;;
esac
if ! project_root=$(CDPATH= cd -- "$project_root" && pwd -P); then
    printf '%s\n' "chibi MCP project root is not a directory: $project_root" >&2
    exit 1
fi
build_dir=${BUILD_DIR:-build}
case "$build_dir" in
    /*) ;;
    *) build_dir="$repo_root/$build_dir" ;;
esac
controller=${CHIBI_CTL:-"$build_dir/chibi_ctl"}

if [ -n "${CHIBI_CTL:-}" ] && [ ! -x "$controller" ]; then
    printf '%s\n' "chibi MCP controller is not executable: $controller" >&2
    exit 1
fi

# A worktree without its own build borrows the shared build of identical native sources.
if [ -z "${CHIBI_CTL:-}" ] && [ ! -x "$controller" ]; then
    cached=$(sh "$script_dir/controller-cache.sh" 2>/dev/null || true)
    if [ -n "$cached" ] && [ -x "$cached" ]; then
        controller=$cached
    fi
fi

if node_bin=$(command -v node 2>/dev/null); then
    :
elif [ -x "/Applications/Codex.app/Contents/Resources/cua_node/bin/node" ]; then
    node_bin="/Applications/Codex.app/Contents/Resources/cua_node/bin/node"
else
    printf '%s\n' 'chibi MCP requires Node.js 22 or newer' >&2
    exit 1
fi
# Controller responses keep 64-bit IDs exact with the Node 22 JSON reviver source.
if ! "$node_bin" -e 'process.exit(Number(process.versions.node.split(".")[0]) >= 22 ? 0 : 1)'; then
    printf '%s\n' "chibi MCP requires Node.js 22 or newer; $node_bin is $("$node_bin" --version)" >&2
    exit 1
fi

# The server answers the handshake immediately and builds chibi_ctl on first use.
printf '%s\n' "chibi MCP starting from project root $project_root" >&2
CHIBI_ROOT="$project_root" \
CHIBI_CTL="$controller" \
CHIBI_RENDER_SHADER_DIR="$repo_root/src/render/shaders" \
exec "$node_bin" "$repo_root/tools/mcp/server.mjs"
