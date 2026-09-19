#!/bin/sh
set -eu

build_dir=${TRACY_MCP_BUILD_DIR:-build-tracy-mcp}
python=${TRACY_MCP_PYTHON:-python3}
source_dir=${TRACY_MCP_SOURCE_DIR:-$build_dir/_deps/tracy-src}

if [ ! -f "$source_dir/extra/mcp/tracy_mcp.py" ]; then
    echo "Tracy source not found at $source_dir; run make build-tracy-mcp first" >&2
    exit 1
fi

binding_file=$(find "$build_dir" -type f \( -name 'TracyServerBindings*.so' -o -name 'TracyServerBindings*.pyd' \) -print -quit)
if [ -z "$binding_file" ]; then
    echo "TracyServerBindings was not built below $build_dir" >&2
    exit 1
fi
binding_dir=$(dirname "$binding_file")
export PYTHONPATH="$binding_dir${PYTHONPATH:+:$PYTHONPATH}"

if ! "$python" -c 'import mcp, TracyServerBindings' >/dev/null 2>&1; then
    echo "Install Tracy MCP dependencies before starting the server:" >&2
    echo "  $python -m pip install -r $source_dir/extra/mcp/requirements.txt" >&2
    exit 1
fi

exec "$python" "$source_dir/extra/mcp/tracy_mcp.py" "$@"
