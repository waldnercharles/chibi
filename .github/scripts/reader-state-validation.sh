#!/usr/bin/env bash
# Only fixed statuses/timing and source/control SHA identities leave the runner.
set +x
exec 3>&1
exec >/dev/null 2>&1
set -euo pipefail
umask 077
environment_valid() {
    # The inspected ELF closure and ordinary flags must be the ones later used.
    local name
    for name in LD_PRELOAD LD_LIBRARY_PATH LD_AUDIT CFLAGS CXXFLAGS CPPFLAGS LDFLAGS \
                LIBRARY_PATH CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH \
                GCC_EXEC_PREFIX COMPILER_PATH CMAKE_TOOLCHAIN_FILE CMAKE_PREFIX_PATH; do
        [[ -z "${!name:-}" ]] || return 1
    done
}

if ! environment_valid || [[ -z "${RUNNER_TEMP:-}" || -z "${GITHUB_WORKSPACE:-}" ]]; then
    printf 'phase=bootstrap exit=1\n' >&3
    exit 1
fi
WORK="$RUNNER_TEMP/chibi-m5-reader-state-private"
PUBLIC="$RUNNER_TEMP/chibi-m5-reader-state-status"
if ! mkdir -p "$WORK" "$PUBLIC" || ! exec >>"$WORK/bootstrap.log" 2>&1; then
    printf 'phase=bootstrap exit=1\n' >&3
    exit 1
fi
CONTROL="$GITHUB_WORKSPACE/validation"
ROOT="$GITHUB_WORKSPACE/chibi"
LANGUAGE_BUILD="$WORK/build-language"
TOOLS_BUILD="$WORK/build-tools-dispatcher"
HOST="$TOOLS_BUILD/sdk/chibi-compiler-host"
DEPS="$WORK/dependencies"
export CC=clang-18 CMAKE_BUILD_PARALLEL_LEVEL=2
SELECTORS=(language-package-snapshot-chibi compiler-artifact
           language-signature-reader-chibi language-snapshot-reader-chibi)
ulimit -c 0

admission_valid() {
    environment_valid || return 1
    [[ "${READER_STATE_REVIEWED:-false}" == true && "${GITHUB_RUN_ATTEMPT:-}" == 1 ]] || return 1
    [[ "${CONTROL_SHA:-}" =~ ^[0-9a-f]{40}$ && "${CANDIDATE_SHA:-}" =~ ^[0-9a-f]{40}$ &&
       "${CANDIDATE_TREE:-}" =~ ^[0-9a-f]{40}$ ]] || return 1
    [[ "${GITHUB_REPOSITORY:-}" == waldnercharles/chibi-studio &&
       "${PR_BASE_SHA:-}" == "${EXPECTED_BASE_SHA:-invalid}" &&
       "${EXPECTED_BASE_SHA:-}" =~ ^[0-9a-f]{40}$ && "${PR_BASE_BRANCH:-}" == main &&
       "${PR_HEAD_BRANCH:-}" == codex/host-m5-unit-validation-20261018 &&
       "${SOURCE_BRANCH:-}" == refs/heads/codex/m5-snapshot-reader-validation-20261018 ]] || return 1
}

checkout_identity() {
    local directory=$1 expected_head=$2 expected_tree=$3 head tree status
    head=$(git -C "$directory" rev-parse HEAD) || return 1
    [[ "$head" == "$expected_head" ]] || return 1
    tree=$(git -C "$directory" rev-parse 'HEAD^{tree}') || return 1
    [[ "$tree" =~ ^[0-9a-f]{40}$ ]] || return 1
    [[ -z "$expected_tree" || "$tree" == "$expected_tree" ]] || return 1
    status=$(git -C "$directory" status --porcelain) || return 1
    [[ -z "$status" ]] || return 1
}

identity() {
    admission_valid || return 1
    checkout_identity "$CONTROL" "$CONTROL_SHA" '' || return 1
    checkout_identity "$ROOT" "$CANDIDATE_SHA" "$CANDIDATE_TREE" || return 1
}

prepare() {
    identity || return 1
    git -C "$CONTROL" rev-parse 'HEAD^{tree}' > "$WORK/control-tree-sha"
}

expected_cases() {
    case "$1" in
      language-package-snapshot-chibi) printf '16\n' ;;
      compiler-artifact) printf '15\n' ;;
      language-signature-reader-chibi) printf '4\n' ;;
      language-snapshot-reader-chibi) printf '3\n' ;;
      *) return 2 ;;
    esac
}

fetch_dependency() {
    local name=$1 url=$2 sha=$3
    git init "$DEPS/$name"
    git -C "$DEPS/$name" remote add origin "$url"
    git -C "$DEPS/$name" fetch --depth=1 origin "$sha"
    git -C "$DEPS/$name" -c advice.detachedHead=false checkout --detach FETCH_HEAD
    [[ "$(git -C "$DEPS/$name" rev-parse HEAD)" == "$sha" ]]
}

dependencies() {
    identity || return 1
    sudo apt-get update
    sudo apt-get install -y --no-install-recommends clang-18 python3-venv git make ca-certificates jq binutils
    python3 -m venv "$WORK/build-tools"
    "$WORK/build-tools/bin/pip" install --only-binary=:all: --require-hashes -r "$ROOT/.github/build-tools.txt"
    printf '%s\n' "$WORK/build-tools/bin" >> "$GITHUB_PATH"
    mkdir -p "$DEPS"
    fetch_dependency tree-sitter https://github.com/tree-sitter/tree-sitter.git da6fe9beb4f7f67beb75914ca8e0d48ae48d6406
    fetch_dependency css https://github.com/tree-sitter/tree-sitter-css.git dda5cfc5722c429eaba1c910ca32c2c0c5bb1a3f
    fetch_dependency html https://github.com/tree-sitter/tree-sitter-html.git 5a5ca8551a179998360b4a4ca2c0f366a35acc03
    fetch_dependency sljit https://github.com/zherczeg/sljit.git a0020dd49a21020434ff838fc308626c48cd8f05
    fetch_dependency cute https://github.com/RandyGaul/cute_framework.git 96e6034aa6bc44665856ee480962a9ddab0b87cc
}

configure() {
    identity || return 1
    local directory source
    for source in language tools; do
        if [[ "$source" == language ]]; then directory=$LANGUAGE_BUILD; else directory=$TOOLS_BUILD; fi
        mkdir -p "$directory/.cmake/api/v1/query"
        touch "$directory/.cmake/api/v1/query/codemodel-v2" \
              "$directory/.cmake/api/v1/query/cache-v2" \
              "$directory/.cmake/api/v1/query/toolchains-v1"
        # These are normal product flags. File API/compile databases are private evidence only.
        cmake -S "$ROOT/test/$source" -B "$directory" -G Ninja \
          -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
          -DCHIBI_SDK_BUILD_COMPILER=ON -DCHIBI_SDK_BUILD_NATIVE=OFF \
          -DCHIBI_SDK_BUILD_DESKTOP=OFF -DCHIBI_ENABLE_SJLIT=ON \
          -DFETCHCONTENT_SOURCE_DIR_CHIBI_LANGUAGE_TREE_SITTER="$DEPS/tree-sitter" \
          -DFETCHCONTENT_SOURCE_DIR_CHIBI_UI_CSS="$DEPS/css" \
          -DFETCHCONTENT_SOURCE_DIR_CHIBI_UI_HTML="$DEPS/html" \
          -DCHIBI_SJLIT_SOURCE_DIR="$DEPS/sljit" \
          -DCHIBI_CUTE_MATH_INCLUDE_DIR="$DEPS/cute/include"
    done
}

build() {
    identity || return 1
    cmake --build "$LANGUAGE_BUILD" --target test_language --parallel 2
    cmake --build "$TOOLS_BUILD" --target test_tools chibi-compiler-host --parallel 2
    # No new target, relink, strip or validation-only compiler/link flags.
    sha256sum "$LANGUAGE_BUILD/test_language" "$TOOLS_BUILD/test_tools" "$HOST" > "$WORK/binary.sha256"
}

audit() {
    identity || return 1
    [[ -f "$WORK/build.exit" && "$(cat "$WORK/build.exit")" == 0 ]] || return 1
    sha256sum -c "$WORK/binary.sha256" || return 1
    python3 "$ROOT/.github/scripts/transport-boundary-audit.py" \
      --source-root "$ROOT" --language-build "$LANGUAGE_BUILD" \
      --tools-build "$TOOLS_BUILD" --private-log "$WORK/boundary-detail.log"
}

execute() {
    expected_cases "${SELECTOR:-}" >/dev/null || return 2
    identity || return 1
    [[ -f "$WORK/audit.exit" && "$(cat "$WORK/audit.exit")" == 0 ]] || return 1
    # One complete invocation of each admitted selector; no subsets or retries.
    mkdir "$WORK/execution-started-$SELECTOR" || return 1
    sha256sum -c "$WORK/binary.sha256" || return 1
    printf '%s\n' "$CANDIDATE_SHA" > "$WORK/identity-$SELECTOR.verified"
    cd "$ROOT"
    CHIBI_LANGUAGE_BUILD_DIR="$LANGUAGE_BUILD" CHIBI_TOOLS_BUILD_DIR="$TOOLS_BUILD" \
    CHIBI_COMPILER_HOST="$HOST" python3 tools/test_policy.py tool "$SELECTOR"
}

classify() {
    local selector=$1 expected outcome=not_started rc summary summary_count supervisor_count
    local log="$WORK/execute-$1.log"
    local identity_verified=false summary_verified=false supervisor=false
    expected=$(expected_cases "$selector") || return 2
    if identity && [[ -f "$WORK/identity-$selector.verified" ]] &&
       [[ "$(cat "$WORK/identity-$selector.verified")" == "$CANDIDATE_SHA" ]]; then
        identity_verified=true
    fi
    if [[ -f "$WORK/execute-$selector.exit" && -r "$log" ]]; then
        rc=$(cat "$WORK/execute-$selector.exit")
        summary_count=$(grep -c '^Summary:' "$log" || :)
        summary=$(grep -E "^Summary: Passed: $expected Failed: 0 Total: $expected Suites: [0-9]{1,12} Asserts: [1-9][0-9]{0,11}$" "$log" || :)
        if [[ "$summary_count" == 1 && -n "$summary" ]]; then summary_verified=true; fi
        supervisor_count=$(grep -c '^Test command wall time:' "$log" || :)
        if [[ "$supervisor_count" == 1 ]] &&
           grep -Eq '^Test command wall time: [0-9]+\.[0-9]{3}s; ceiling 60s; includes in-test fixture setup; external configuration/build excluded\.$' "$log" &&
           awk '/^Test command wall time:/ { if (($5 + 0) > 60) exit 1 }' "$log" &&
           ! grep -Eq '^FAIL:|AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$log"; then
            supervisor=true
        fi
        case "$rc" in
          0)
            outcome=unexpected_test_result
            if [[ "$identity_verified:$summary_verified:$supervisor" == true:true:true ]]; then outcome=passed; fi ;;
          124) outcome=supervisor_timeout ;;
          1) outcome=unexpected_test_failure ;;
          *) outcome=runtime_or_supervisor_failure ;;
        esac
    fi
    [[ "$identity_verified" == true ]] || outcome=identity_unverified
    jq -n --arg selector "$selector" --arg outcome "$outcome" \
      --argjson identity "$identity_verified" --argjson summary "$summary_verified" --argjson supervisor "$supervisor" \
      '{selector:$selector,outcome:$outcome,execution_identity_verified:$identity,
        expected_pico_summary_verified:$summary,production_supervisor_60s_report_verified:$supervisor}' \
      > "$WORK/result-$selector.json"
}

collect() {
    local phase selector rc elapsed control_tree
    admission_valid || return 1
    : > "$WORK/results.jsonl"
    for selector in "${SELECTORS[@]}"; do
        classify "$selector"
        cat "$WORK/result-$selector.json" >> "$WORK/results.jsonl"
    done
    local phases=(prepare dependencies configure build audit)
    for selector in "${SELECTORS[@]}"; do phases+=("execute-$selector"); done
    : > "$WORK/phases.jsonl"
    for phase in "${phases[@]}"; do
        [[ -f "$WORK/$phase.exit" && -f "$WORK/$phase.seconds" ]] || continue
        rc=$(cat "$WORK/$phase.exit"); elapsed=$(cat "$WORK/$phase.seconds")
        [[ "$rc" =~ ^(0|[1-9][0-9]{0,2})$ && "$elapsed" =~ ^(0|[1-9][0-9]{0,7})$ ]] || return 1
        jq -n --arg phase "$phase" --argjson exit "$rc" --argjson seconds "$elapsed" \
          '{phase:$phase,exit:$exit,elapsed_seconds:$seconds}' >> "$WORK/phases.jsonl"
    done
    control_tree=$(cat "$WORK/control-tree-sha") || return 1
    [[ "$control_tree" =~ ^[0-9a-f]{40}$ ]] || return 1
    jq -s '.' "$WORK/phases.jsonl" > "$WORK/phases.json"
    jq -s '.' "$WORK/results.jsonl" > "$WORK/results.json"
    jq -n --arg source_sha "$CANDIDATE_SHA" --arg source_tree "$CANDIDATE_TREE" \
      --arg control_sha "$CONTROL_SHA" --arg control_tree "$control_tree" \
      --slurpfile results "$WORK/results.json" --slurpfile phases "$WORK/phases.json" \
      '{schema:1,identity:{source_sha:$source_sha,source_tree:$source_tree,control_sha:$control_sha,control_tree:$control_tree},
        phases:$phases[0],results:$results[0],raw_logs:"runner_local_discarded",test_ceiling_seconds:60}' > "$PUBLIC/status.json"
}

verdict() {
    local control_tree
    identity || return 1
    sha256sum -c "$WORK/binary.sha256" >/dev/null || return 1
    [[ -f "$WORK/collect.exit" && "$(cat "$WORK/collect.exit")" == 0 ]] || return 1
    control_tree=$(git -C "$CONTROL" rev-parse 'HEAD^{tree}') || return 1
    jq -e --arg source "$CANDIDATE_SHA" --arg tree "$CANDIDATE_TREE" \
      --arg control "$CONTROL_SHA" --arg control_tree "$control_tree" '
      .schema == 1 and .test_ceiling_seconds == 60 and
      .identity.source_sha == $source and .identity.source_tree == $tree and .identity.control_sha == $control and
      .identity.control_tree == $control_tree and
      ((.phases | map(.phase) | sort) == ([
        "prepare", "dependencies", "configure", "build", "audit",
        "execute-language-package-snapshot-chibi", "execute-compiler-artifact",
        "execute-language-signature-reader-chibi", "execute-language-snapshot-reader-chibi"
      ] | sort)) and
      all(.phases[]; .exit == 0 and (.elapsed_seconds | type == "number" and . >= 0)) and
      ((.results | map(.selector) | sort) == ([
        "language-package-snapshot-chibi", "compiler-artifact",
        "language-signature-reader-chibi", "language-snapshot-reader-chibi"
      ] | sort)) and
      all(.results[]; .outcome == "passed" and .execution_identity_verified == true and
        .expected_pico_summary_verified == true and .production_supervisor_60s_report_verified == true)
    ' "$PUBLIC/status.json" >/dev/null
}

phase=${1:-invalid}
case "$phase" in prepare|dependencies|configure|build|audit|execute|collect|verdict) ;; *) printf 'phase=invalid exit=2\n' >&3; exit 2 ;; esac
key=$phase
if [[ "$phase" == execute ]]; then
    expected_cases "${SELECTOR:-}" >/dev/null || { printf 'phase=execute exit=2\n' >&3; exit 2; }
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
