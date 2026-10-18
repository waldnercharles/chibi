#!/usr/bin/env bash
# Draft: only fixed status/timing and SHA identities leave this runner.
set +x
set -euo pipefail
umask 077
WORK="${RUNNER_TEMP:?}/chibi-native-unit-private"
PUBLIC="$RUNNER_TEMP/chibi-native-unit-status"
mkdir -p "$WORK" "$PUBLIC"
exec 3>&1
exec >>"$WORK/bootstrap.log" 2>&1
export CC=clang-18 CXX=clang++-18 CMAKE_BUILD_PARALLEL_LEVEL=2
ROOT="${GITHUB_WORKSPACE:?}/chibi"
CONTROL="$GITHUB_WORKSPACE/validation"
DEPS="$WORK/dependencies"
REFLECT_BUILD="$WORK/reflect"
TOOLS_BUILD="$WORK/tools"
LANGUAGE_BUILD="$WORK/language"
ulimit -c 0

identity() {
    local status
    [[ "$CANDIDATE_SHA" =~ ^[0-9a-f]{40}$ && "$CANDIDATE_TREE" =~ ^[0-9a-f]{40}$ ]] || return 1
    [[ "$(git -C "$ROOT" rev-parse HEAD)" == "$CANDIDATE_SHA" ]] || return 1
    [[ "$(git -C "$ROOT" rev-parse 'HEAD^{tree}')" == "$CANDIDATE_TREE" ]] || return 1
    status=$(git -C "$ROOT" status --porcelain) || return 1
    [[ -z "$status" ]] || return 1
}
prepare() {
    local control_status
    [[ "$NATIVE_UNIT_REVIEWED" == true ]]
    [[ "$GITHUB_REPOSITORY" == waldnercharles/chibi-studio ]]
    [[ "$PR_BASE_SHA" == bee5b54e159cce44ee52ca9439be587d9a95f5d2 && "$PR_BASE_BRANCH" == main ]]
    [[ "$PR_HEAD_BRANCH" == codex/host-m5-unit-validation-20261018 ]]
    [[ "$CONTROL_SHA" =~ ^[0-9a-f]{40}$ ]]
    [[ "$(git -C "$CONTROL" rev-parse HEAD)" == "$CONTROL_SHA" ]]
    control_status=$(git -C "$CONTROL" status --porcelain) || return 1
    [[ -z "$control_status" ]] || return 1
    identity
}
dependencies() {
    sudo apt-get update
    sudo apt-get install -y --no-install-recommends clang-18 libclang-18-dev libclang-rt-18-dev \
      python3-venv git make ca-certificates jq binutils pkg-config libssl-dev zlib1g-dev \
      libgl1-mesa-dev libvulkan1 mesa-vulkan-drivers libx11-dev libxcursor-dev \
      libxext-dev libxi-dev libxrandr-dev libxss-dev libxtst-dev libwayland-dev \
      libxkbcommon-dev libasound2-dev libpulse-dev
    python3 -m venv "$WORK/build-tools"
    "$WORK/build-tools/bin/pip" install --only-binary=:all: --require-hashes -r "$ROOT/.github/build-tools.txt"
    printf '%s\n' "$WORK/build-tools/bin" >> "$GITHUB_PATH"
    mkdir -p "$DEPS"
    local name url sha
    while read -r name url sha; do
        git init "$DEPS/$name"
        git -C "$DEPS/$name" remote add origin "$url"
        git -C "$DEPS/$name" fetch --depth=1 origin "$sha"
        git -C "$DEPS/$name" -c advice.detachedHead=false checkout --detach FETCH_HEAD
        [[ "$(git -C "$DEPS/$name" rev-parse HEAD)" == "$sha" ]]
    done < "$ROOT/.github/native-unit-dependencies.tsv"
    test -f /usr/lib/llvm-18/lib/libclang.so
    test -f /usr/lib/llvm-18/include/clang-c/Index.h
}
common_arguments() {
    COMMON=(-G Ninja -DCMAKE_C_COMPILER=clang-18 -DCMAKE_BUILD_TYPE=Debug
      -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF -DCHIBI_SDK_BUILD_COMPILER=ON
      -DCHIBI_ENABLE_SJLIT=ON -DCHIBI_CUTE_MATH_INCLUDE_DIR="$DEPS/cute/include"
      -DCHIBI_SJLIT_SOURCE_DIR="$DEPS/sljit"
      -DFETCHCONTENT_SOURCE_DIR_CHIBI_LANGUAGE_TREE_SITTER="$DEPS/tree-sitter"
      -DFETCHCONTENT_SOURCE_DIR_CHIBI_UI_CSS="$DEPS/css"
      -DFETCHCONTENT_SOURCE_DIR_CHIBI_UI_HTML="$DEPS/html"
      '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
      '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
      '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
}
configure_reflect() {
    identity
    cmake -S "$ROOT/tools/reflect" -B "$REFLECT_BUILD" -G Ninja \
      -DCMAKE_C_COMPILER=clang-18 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF -DLIBCLANG_ROOT=/usr/lib/llvm-18 \
      -DLIBCLANG_LIBRARY=/usr/lib/llvm-18/lib/libclang.so \
      -DLIBCLANG_INCLUDE_DIR=/usr/lib/llvm-18/include \
      -DCHIBI_SDK_BUILD_NATIVE=OFF -DCHIBI_SDK_BUILD_DESKTOP=OFF
}
build_reflect() {
    cmake --build "$REFLECT_BUILD" --target reflect --parallel 2
}
configure_tools() {
    identity
    common_arguments
    local resource
    resource=$(/usr/lib/llvm-18/bin/clang -print-resource-dir)
    test -f "$resource/include/stdbool.h"
    cmake -S "$ROOT/test/tools" -B "$TOOLS_BUILD" "${COMMON[@]}" \
      -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
      -DCHIBI_SDK_BUILD_NATIVE=ON -DCHIBI_SDK_BUILD_DESKTOP=ON \
      -DCHIBI_REFLECT_EXECUTABLE="$REFLECT_BUILD/reflect" \
      -DCHIBI_REFLECT_RESOURCE_INCLUDE_DIR="$resource/include" \
      -DCHIBI_ENABLE_TRACY=OFF -DCF_WEBGPU=OFF -DCF_USE_SYSTEM_SDL=OFF \
      -DFETCHCONTENT_SOURCE_DIR_CUTE_FRAMEWORK="$DEPS/cute" \
      -DFETCHCONTENT_SOURCE_DIR_CHIBI_BOX2D_SOURCE="$DEPS/box2d" \
      -DFETCHCONTENT_SOURCE_DIR_CHIBI_BOX3D_SOURCE="$DEPS/box3d" \
      -DFETCHCONTENT_SOURCE_DIR_CHIBI_ENKITS_SOURCE="$DEPS/enkits" \
      -DFETCHCONTENT_SOURCE_DIR_BOX2D="$DEPS/box2d" \
      -DFETCHCONTENT_SOURCE_DIR_BOX3D="$DEPS/box3d" \
      -DFETCHCONTENT_SOURCE_DIR_ENKITS="$DEPS/enkits" \
      -DFETCHCONTENT_SOURCE_DIR_SDL3="$DEPS/sdl3" \
      -DFETCHCONTENT_SOURCE_DIR_S2N="$DEPS/s2n" \
      -DFETCHCONTENT_SOURCE_DIR_PHYSFS="$DEPS/physfs" \
      -DFETCHCONTENT_SOURCE_DIR_FREETYPE="$DEPS/freetype" \
      -DFETCHCONTENT_SOURCE_DIR_RMLUI="$DEPS/rmlui"
}
verify_binary() {
    local binary=$1 tag=$2
    # Evidence of linked instrumentation, kept private; not a blanket coverage claim.
    nm "$binary" > "$WORK/$tag.symbols"
    grep -Eq '[[:space:]]__asan_init$' "$WORK/$tag.symbols"
    grep -Eq '[[:space:]]__ubsan_handle_' "$WORK/$tag.symbols"
    sha256sum "$binary" > "$WORK/$tag.sha256"
}
build_tools() {
    cmake --build "$TOOLS_BUILD" --target test_tools --parallel 2
    verify_binary "$TOOLS_BUILD/test_tools" tools
}
configure_language() {
    identity
    common_arguments
    cmake -S "$ROOT/test/language" -B "$LANGUAGE_BUILD" "${COMMON[@]}" \
      -DCHIBI_SDK_BUILD_NATIVE=OFF -DCHIBI_SDK_BUILD_DESKTOP=OFF
}
build_language() {
    cmake --build "$LANGUAGE_BUILD" --target test_language --parallel 2
    verify_binary "$LANGUAGE_BUILD/test_language" language
}
execute() {
    identity
    case "$SELECTOR" in
      language-semantic|language-incremental|language-unit-analysis-chibi) sha256sum -c "$WORK/language.sha256" ;;
      unit-fixtures) sha256sum -c "$WORK/tools.sha256" ;;
      *) return 2 ;;
    esac
    cd "$ROOT"
    CHIBI_LANGUAGE_BUILD_DIR="$LANGUAGE_BUILD" CHIBI_TOOLS_BUILD_DIR="$TOOLS_BUILD" \
      python3 tools/test_policy.py tool "$SELECTOR"
}
collect() {
    local phase rc seconds selector expected summary outcome
    : > "$WORK/phases.jsonl"
    for phase in prepare dependencies configure_reflect build_reflect configure_tools build_tools configure_language build_language execute-language-semantic execute-language-incremental execute-unit-fixtures execute-language-unit-analysis-chibi; do
        [[ -f "$WORK/$phase.exit" ]] || continue
        rc=$(cat "$WORK/$phase.exit"); seconds=$(cat "$WORK/$phase.seconds")
        [[ "$rc" =~ ^[0-9]{1,3}$ && "$seconds" =~ ^[0-9]{1,8}$ ]]
        jq -n --arg phase "$phase" --argjson rc "$rc" --argjson seconds "$seconds" \
          '{phase:$phase,exit:$rc,elapsed_seconds:$seconds}' >> "$WORK/phases.jsonl"
    done
    : > "$WORK/results.jsonl"
    for selector in language-semantic language-incremental unit-fixtures language-unit-analysis-chibi; do
        case "$selector" in language-semantic) expected=42 ;; language-incremental) expected=13 ;; unit-fixtures) expected=2 ;; language-unit-analysis-chibi) expected=3 ;; esac
        outcome=not_started
        if [[ -f "$WORK/execute-$selector.exit" ]]; then
            rc=$(cat "$WORK/execute-$selector.exit")
            case "$rc" in 124) outcome=supervisor_timeout ;; 0) outcome=unexpected_summary ;; *) outcome=test_or_supervisor_failure ;; esac
            summary=$(grep -Ec "^Summary: Passed: $expected Failed: 0 Total: $expected Suites: [0-9]+ Asserts: [1-9][0-9]*$" "$WORK/execute-$selector.log" || :)
            if [[ "$rc:$summary" == 0:1 ]] &&
               [[ $(grep -c '^Summary:' "$WORK/execute-$selector.log") == 1 ]] &&
               grep -Eq '^Test command wall time: [0-9.]+s; ceiling 60s;' "$WORK/execute-$selector.log" &&
               ! grep -Eq '^FAIL:|AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$WORK/execute-$selector.log"; then
                outcome=passed
            fi
        fi
        jq -n --arg selector "$selector" --arg outcome "$outcome" \
          '{selector:$selector,outcome:$outcome}' >> "$WORK/results.jsonl"
    done
    [[ "$CANDIDATE_SHA" =~ ^[0-9a-f]{40}$ && "$CANDIDATE_TREE" =~ ^[0-9a-f]{40}$ && "$CONTROL_SHA" =~ ^[0-9a-f]{40}$ ]]
    local identity_verified=false control_tree
    if identity; then identity_verified=true; fi
    control_tree=$(git -C "$CONTROL" rev-parse 'HEAD^{tree}')
    [[ "$control_tree" =~ ^[0-9a-f]{40}$ ]]
    jq -s . "$WORK/phases.jsonl" > "$WORK/phases.json"
    jq -s . "$WORK/results.jsonl" > "$WORK/results.json"
    jq -n --arg source_sha "$CANDIDATE_SHA" --arg source_tree "$CANDIDATE_TREE" \
      --arg control_sha "$CONTROL_SHA" --arg control_tree "$control_tree" \
      --argjson verified "$identity_verified" --slurpfile phases "$WORK/phases.json" --slurpfile results "$WORK/results.json" \
      '{identity:{source_sha:$source_sha,source_tree:$source_tree,control_sha:$control_sha,control_tree:$control_tree,verified:$verified},
        phases:$phases[0],results:$results[0],test_ceiling_seconds:60,raw_logs:"runner_local_discarded"}' > "$PUBLIC/status.json"
}
verdict() {
    jq -e '
      .identity.verified == true and .test_ceiling_seconds == 60 and
      ((.phases | map(.phase) | sort) == ([
        "prepare", "dependencies", "configure_reflect", "build_reflect",
        "configure_tools", "build_tools", "configure_language", "build_language",
        "execute-language-semantic", "execute-language-incremental",
        "execute-unit-fixtures", "execute-language-unit-analysis-chibi"
      ] | sort)) and
      ((.results | map(.selector) | sort) == ([
        "language-semantic", "language-incremental", "unit-fixtures",
        "language-unit-analysis-chibi"
      ] | sort)) and
      all(.results[]; .outcome == "passed") and all(.phases[]; .exit == 0)
    ' "$PUBLIC/status.json" >/dev/null
}
phase=${1:-invalid}
case "$phase" in prepare|dependencies|configure_reflect|build_reflect|configure_tools|build_tools|configure_language|build_language|execute|collect|verdict) ;; *) printf 'phase=invalid exit=2\n' >&3; exit 2 ;; esac
key=$phase
if [[ "$phase" == execute ]]; then
    case "${SELECTOR:-}" in language-semantic|language-incremental|unit-fixtures|language-unit-analysis-chibi) ;; *) printf 'phase=execute exit=2\n' >&3; exit 2 ;; esac
    key="execute-$SELECTOR"
fi
started=$SECONDS
set +e
(set -euo pipefail; "$phase") 3>&- >"$WORK/$key.log" 2>&1
rc=$?
set -e
elapsed=$((SECONDS-started))
printf '%s\n' "$rc" > "$WORK/$key.exit"
printf '%s\n' "$elapsed" > "$WORK/$key.seconds"
printf 'phase=%s exit=%d elapsed_seconds=%d\n' "$key" "$rc" "$elapsed" >&3
exit "$rc"
