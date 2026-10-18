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
                GCC_EXEC_PREFIX COMPILER_PATH CMAKE_TOOLCHAIN_FILE CMAKE_PREFIX_PATH \
                ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS ASAN_SYMBOLIZER_PATH; do
        [[ -z "${!name:-}" ]] || return 1
    done
}

if ! environment_valid || [[ -z "${RUNNER_TEMP:-}" || -z "${GITHUB_WORKSPACE:-}" ]]; then
    printf 'phase=bootstrap exit=1\n' >&3
    exit 1
fi
WORK="$RUNNER_TEMP/chibi-m5-package-cache-private"
PUBLIC="$RUNNER_TEMP/chibi-m5-package-cache-status"
if ! mkdir -p "$WORK" "$PUBLIC" || ! exec >>"$WORK/bootstrap.log" 2>&1; then
    printf 'phase=bootstrap exit=1\n' >&3
    exit 1
fi
CONTROL="$GITHUB_WORKSPACE/validation"
ROOT="$GITHUB_WORKSPACE/chibi"
BUILD="$WORK/build-language"
DEPS="$WORK/dependencies"
export CC=clang-18 CMAKE_BUILD_PARALLEL_LEVEL=2
SELECTORS=(language-package-imports)
ulimit -c 0

admission_valid() {
    environment_valid || return 1
    [[ "${PACKAGE_CACHE_REVIEWED:-false}" == true && "${GITHUB_RUN_ATTEMPT:-}" == 1 ]] || return 1
    [[ "${CONTROL_SHA:-}" =~ ^[0-9a-f]{40}$ && "${CANDIDATE_SHA:-}" =~ ^[0-9a-f]{40}$ &&
       "${CANDIDATE_TREE:-}" =~ ^[0-9a-f]{40}$ ]] || return 1
    [[ "${GITHUB_REPOSITORY:-}" == waldnercharles/chibi-studio &&
       "${PR_BASE_SHA:-}" == "${EXPECTED_BASE_SHA:-invalid}" &&
       "${EXPECTED_BASE_SHA:-}" =~ ^[0-9a-f]{40}$ && "${PR_BASE_BRANCH:-}" == main &&
       "${PR_HEAD_BRANCH:-}" == codex/host-m5-unit-validation-20261018 &&
       "${SOURCE_BRANCH:-}" == refs/heads/codex/m5-package-cache-closure-20261018 ]] || return 1
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
    [[ "$1" == language-package-imports ]] || return 2
    printf '12\n'
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
    sudo apt-get install -y --no-install-recommends clang-18 libclang-rt-18-dev python3-venv git make ca-certificates jq binutils
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
    cmake -S "$ROOT/test/language" -B "$BUILD" -G Ninja \
      -DCMAKE_C_COMPILER=clang-18 -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DCHIBI_SDK_BUILD_COMPILER=ON -DCHIBI_SDK_BUILD_NATIVE=OFF \
      -DCHIBI_SDK_BUILD_DESKTOP=OFF -DCHIBI_ENABLE_SJLIT=ON \
      '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
      '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' \
      -DFETCHCONTENT_SOURCE_DIR_CHIBI_LANGUAGE_TREE_SITTER="$DEPS/tree-sitter" \
      -DFETCHCONTENT_SOURCE_DIR_CHIBI_UI_CSS="$DEPS/css" \
      -DFETCHCONTENT_SOURCE_DIR_CHIBI_UI_HTML="$DEPS/html" \
      -DCHIBI_SJLIT_SOURCE_DIR="$DEPS/sljit" \
      -DCHIBI_CUTE_MATH_INCLUDE_DIR="$DEPS/cute/include"
}

build() {
    identity || return 1
    [[ -f "$WORK/configure.exit" && "$(cat "$WORK/configure.exit")" == 0 ]] || return 1
    cmake --build "$BUILD" --target test_language --parallel 2
}

verify() {
    identity || return 1
    [[ -f "$WORK/build.exit" && "$(cat "$WORK/build.exit")" == 0 ]] || return 1
    # Inspect real generated inputs privately; labels alone are insufficient.
    ninja -C "$BUILD" -t commands test_language > "$WORK/language.commands"
    python3 -I -B - "$ROOT" "$BUILD" "$DEPS" <<'VERIFY_BUILD'
import json, pathlib, shlex, sys
root, build, deps = map(pathlib.Path, sys.argv[1:])
cache = {}
for line in (build / 'CMakeCache.txt').read_text().splitlines():
    if not line or line.startswith(('#', '//')) or '=' not in line:
        continue
    key, value = line.split('=', 1)
    cache[key.split(':', 1)[0]] = value
for key, value in {
    'CMAKE_BUILD_TYPE': 'Debug', 'CMAKE_GENERATOR': 'Ninja',
    'CMAKE_HOME_DIRECTORY': str(root / 'test/language'),
    'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON', 'CHIBI_ENABLE_SJLIT': 'ON',
    'CHIBI_SDK_BUILD_COMPILER': 'ON', 'CHIBI_SDK_BUILD_NATIVE': 'OFF',
    'CHIBI_SDK_BUILD_DESKTOP': 'OFF', 'CHIBI_SJLIT_SOURCE_DIR': str(deps / 'sljit'),
    'CMAKE_C_FLAGS': '-fsanitize=address,undefined -fno-omit-frame-pointer',
    'CMAKE_C_FLAGS_DEBUG': '-g',
    'CMAKE_EXE_LINKER_FLAGS': '-fsanitize=address,undefined',
}.items():
    assert cache.get(key) == value, key
assert pathlib.Path(cache['CMAKE_C_COMPILER']).resolve() == pathlib.Path('/usr/bin/clang-18').resolve()
commands = json.loads((build / 'compile_commands.json').read_text())
assert commands
sources = {}
for item in commands:
    args = item.get('arguments') or shlex.split(item['command'])
    assert '-fsanitize=address,undefined' in args and '-fno-omit-frame-pointer' in args
    assert not any(x.startswith(('-fno-sanitize', '-DNDEBUG', '-DCHIBI_HAVE_SJLIT=0')) for x in args)
    sources.setdefault(pathlib.Path(item['file']).resolve(), []).append(args)
for suffix in ('test/language/main.c', 'test/language/package_imports.c'):
    entries = sources.get((root / suffix).resolve(), [])
    assert len(entries) == 1 and '-UNDEBUG' in entries[0], suffix
jit = sources.get((root / 'src/execution/jit.c').resolve(), [])
assert len(jit) == 1 and '-DCHIBI_HAVE_SJLIT=1' in jit[0]
assert (build / 'sdk/private/sljit/sljitLir.c').resolve() in sources
links = []
for line in (build.parent / 'language.commands').read_text().splitlines():
    args = shlex.split(line)
    if '-o' in args and args[args.index('-o') + 1] == 'test_language':
        links.append(args)
assert len(links) == 1
assert '-fsanitize=address,undefined' in links[0]
assert any(x.endswith('/libchibi_execution_sljit.a') for x in links[0])
assert not any(x.startswith('-fno-sanitize') for x in links[0])
VERIFY_BUILD
    # Reuse native validation's linked instrumentation checks; no raw symbol export.
    nm "$BUILD/test_language" > "$WORK/language.symbols"
    grep -Eq '[[:space:]]__asan_init$' "$WORK/language.symbols"
    grep -Eq '[[:space:]]__ubsan_handle_' "$WORK/language.symbols"
    grep -Eq '[[:space:]]sljit_generate_code$' "$WORK/language.symbols"
    sha256sum "$BUILD/test_language" "$BUILD/CMakeCache.txt" \
      "$BUILD/compile_commands.json" "$BUILD/build.ninja" > "$WORK/binary.sha256"
}

execute() {
    expected_cases "${SELECTOR:-}" >/dev/null || return 2
    identity || return 1
    [[ -f "$WORK/verify.exit" && "$(cat "$WORK/verify.exit")" == 0 ]] || return 1
    # One complete invocation of each admitted selector; no subsets or retries.
    mkdir "$WORK/execution-started-$SELECTOR" || return 1
    sha256sum -c "$WORK/binary.sha256" || return 1
    printf '%s\n' "$CANDIDATE_SHA" > "$WORK/identity-$SELECTOR.verified"
    cd "$ROOT"
    CHIBI_LANGUAGE_BUILD_DIR="$BUILD" python3 -I -B tools/test_policy.py tool "$SELECTOR"
}

classify() {
    local selector=$1 expected outcome=not_started rc summary summary_count supervisor_count
    local log="$WORK/execute-$1.log"
    local identity_verified=false summary_verified=false supervisor=false diagnostic=none
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
    if [[ "$outcome" != passed && -r "$log" ]]; then
        if grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$log"; then
            diagnostic=sanitizer_failure
        elif grep -Fq '(FAILED:' "$log"; then
            diagnostic=assertion_failure
        else
            diagnostic=failure_unclassified
        fi
    fi
    jq -n --arg selector "$selector" --arg outcome "$outcome" --arg diagnostic "$diagnostic" \
      --argjson identity "$identity_verified" --argjson summary "$summary_verified" --argjson supervisor "$supervisor" \
      '{selector:$selector,outcome:$outcome,diagnostic:$diagnostic,execution_identity_verified:$identity,
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
    local phases=(prepare dependencies configure build verify)
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
        "prepare", "dependencies", "configure", "build", "verify",
        "execute-language-package-imports"
      ] | sort)) and
      all(.phases[]; .exit == 0 and (.elapsed_seconds | type == "number" and . >= 0)) and
      ((.results | map(.selector) | sort) == ([
        "language-package-imports"
      ] | sort)) and
      all(.results[]; .outcome == "passed" and .execution_identity_verified == true and
        .expected_pico_summary_verified == true and .production_supervisor_60s_report_verified == true)
    ' "$PUBLIC/status.json" >/dev/null
}

phase=${1:-invalid}
case "$phase" in prepare|dependencies|configure|build|verify|execute|collect|verdict) ;; *) printf 'phase=invalid exit=2\n' >&3; exit 2 ;; esac
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
