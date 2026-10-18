# Studio formatting review and refreshed startup proposal

Read-only preparation/review by `/root/outliner_authored_projection`, 2026-10-18.
No shared source edits, formatter execution, build, test, Studio launch or asset
format/migration was performed by this reviewer. The parent applied formatting
while this review was in progress; the applied result is reviewed below.

## Formatting scope and result

Studio HEAD is `95ca95bc3ca2da5b899ec06596982ade26d2d94f`; the task changes
remain in the working tree, with no staged Studio delta observed. Studio
AGENTS.md requires engine clang-format 22 on changed lines of existing files.
The actual formatter is
`/workspace/.cloud-tools/python/lib/python3.12/site-packages/clang_format/data/bin/clang-format`,
SHA-256 `fe1bf9c31fc1ea3b16e6c805a9c9182b6e0c8d24898d36b0ba94cfb787ad1a59`.
The engine style `/workspace/chibi-outliner-m3/.clang-format` has SHA-256
`42cada4aef70355adf7d1c4b5f36a19332224f56a201a70235e89ada159fd7e1`.

Exact modified tracked C/H inventory (six):

1. `src/editor/documents/scene_document.c`
2. `src/editor/studio/studio_host.c`
3. `src/editor/studio/studio_ui.c`
4. `src/studio_registry.c`
5. `tests/acceptance/main.c`
6. `tests/acceptance/outliner_row_policies.h`

Exact new, untracked C/H inventory (ten; whole-file formatting is appropriate):

1. `src/editor/studio/studio_outliner_projection.c`
2. `src/editor/studio/studio_outliner_projection.h`
3. `src/editor/studio/studio_outliner_work.c`
4. `src/editor/studio/studio_outliner_work.h`
5. `tests/acceptance/outliner_live.h`
6. `tests/acceptance/outliner_live_fuel.h`
7. `tests/acceptance/outliner_live_guards.h`
8. `tests/acceptance/outliner_projection.h`
9. `tests/acceptance/outliner_scale.h`
10. `tests/acceptance/outliner_work.h`

Do not whole-format the six older files or use the engine's whole-file staged
precommit script as a substitute for Studio's changed-line rule. CMake, JSON,
Markdown and authored `.chibi` are outside this C formatter inventory.

The installed git-clang-format implementation confirms that a single explicit
HEAD baseline formats changed ranges from the current working file and uses a
temporary index. For reproducibility, the suitable command for the five normal
tracked files is the following (the parent has already performed this step;
it is not a request to repeat it):

```sh
cd /workspace/chibi-studio-outliner
/workspace/.cloud-tools/python/bin/git-clang-format \
  --binary=/workspace/.cloud-tools/python/lib/python3.12/site-packages/clang_format/data/bin/clang-format \
  --style=file:/workspace/chibi-outliner-m3/.clang-format \
  --force HEAD -- \
  src/editor/documents/scene_document.c \
  src/editor/studio/studio_host.c \
  src/editor/studio/studio_ui.c \
  src/studio_registry.c \
  tests/acceptance/outliner_row_policies.h
```

`--force` permits this formatter to replace the unstaged files with their
formatted working-copy contents; it does not reset them to HEAD. The exact
same command with `--diff` instead of `--force` previews the formatting patch.
Inspect the output: the tool may return 1 when changes exist. It is not a test
runner. Preserve the current backups and avoid concurrent edits during apply.

`tests/acceptance/main.c` needs the same command with only that file and a
formatter wrapper passing `--sort-includes=false`. Its include order is
semantically significant: projection precedes scale, and live precedes guards
and fuel. The parent used
`/workspace/outliner-build/format-studio-1/clang-format-keep-includes.sh`:

```sh
#!/bin/sh
exec clang-format --sort-includes=false "$@"
```

Pin its PATH resolution to the clang-format 22 binary above when reusing it;
an absolute executable in a future wrapper is equally suitable. No include
sorting exception is needed for the other five tracked files. For the ten new
files, the ordinary absolute clang-format binary with
`--style=file:/workspace/chibi-outliner-m3/.clang-format -i <explicit files>`
is sufficient; no git add or staging is required for untracked files.

### Applied delta accepted

The parent's backup/manifest directory is
`/workspace/outliner-build/format-studio-1`. Its manifest.json SHA-256 is
`8d35ce017004ee0bb6434ee12a258c775f8b98c56ef35319541c4cdb67c8513f`.
All sixteen files were compared with the backups: only these six differ, and
all recorded after hashes match the shared checkout:

| File | Formatted SHA-256 |
| --- | --- |
| src/editor/studio/studio_ui.c | `bf73ac7f7baf4253601b5e61b3d4ddebb6ba880801b755dc5c61ec4998b69787` |
| src/studio_registry.c | `b7681bcf692074d60347c51b23d3ae5a5a5b0c2cc3b549fe29324e6c5ded028a` |
| tests/acceptance/main.c | `d4fb3fc108f1e3017d936ed77c6aec0bd3736b21e07306991de73daa6c2465c2` |
| src/editor/studio/studio_outliner_projection.c | `f343e1771d312f80f89b74212f287da878860088de7684752178e908a2bb063e` |
| src/editor/studio/studio_outliner_projection.h | `6dfe6c50cdce1dfd179c65346f11485c64086116ca11a3f0bfa4d6f5bd12ef47` |
| tests/acceptance/outliner_live.h | `0da0e553eed1bdb05e312b8eb14dc5bff85ebe12fb0caad1d9de14c44b435b14` |

Each exact diff was inspected. The only non-whitespace token differences are:

- Registry swaps the two independent, guarded, self-contained Studio schema
  includes into lexical order. Their typedefs/macros do not depend on order.
- Main splits one usage literal into adjacent literals at the space before
  `--source PATH`; compile-time concatenation preserves every byte, including
  the final newline. Its fixture include block exactly retains its old order.
- Adapter splits the denied-Capture message into adjacent literals at the
  space before `producer.`; concatenation preserves every byte.

All remaining differences are whitespace/line wrapping. No control flow,
argument order, field order, literal workload, source-edit string, arithmetic,
K249 value, grants, publication/authority checks, failure oracle or counter
changed. The fuel fixture, arithmetic fixture and work implementation remain
byte-identical to their accepted hashes. Authored source identities remain
`510f2155...` and `e2c72739...`; formatting does not require a new K census.
This exact formatting delta is accepted before the SDK10 consumer run; it
does not claim an executed build/test or waive the independent run gates.

### EXTRACTION ownership

Three formatted/inventory C files have entries under EXTRACTION.files and
must carry current consumer hashes even where also marked consumer-owned:

| File | Required/current consumer_sha256 |
| --- | --- |
| src/editor/documents/scene_document.c | `3b9b4038626d53c50ec0425304553ade615da0e0cb1e43f31b3bb8bc7b709243` |
| src/editor/studio/studio_host.c | `12ccc912c1b73dd8752926b95e801c5f545ca8da397d9800ff999df6e56b5703` |
| src/editor/studio/studio_ui.c | `bf73ac7f7baf4253601b5e61b3d4ddebb6ba880801b755dc5c61ec4998b69787` |

The modified authored OutlinerRows also has the correct
`510f2155a49ff77724efd38be5d1098f5ac5957c62acb0c06eb755de22e8a450`
consumer hash. All four were verified against current file SHA-256s. Main and
studio_registry are consumer-owned but have no extracted-file entry; the
row-policy helper header, ten new C/H files, new library, source-list CMake and
new evidence files have no applicable extracted entry. Do not invent entries
for new Studio-only files. Current EXTRACTION.json SHA-256 is
`165666a640ca20334a2b7c3ef1ee5a72bb42c2d23ab69ad20c5cd5e9dbad880a`.
No further extraction refresh is needed for this frozen formatting result.
Do not run the broad sync_sources tool: it would refresh unrelated content.

## Fresh SDK10/Studio15 startup preparation — not executed

The parent subsequently authorized preparation after SDK10 and the Studio15
host completed. The fresh directory now exists with the exact copied binary,
resources and project; see `startup-native-2/README.md` for final identities.
No application or test was launched. The preparation commands below record
the bounded setup, rather than requesting that it be repeated.

Keep `/workspace/outliner-build/startup-native` untouched. Its admitted run
exited 0 and produced a valid 1920x1080 PNG but failed six UBSan diagnostics;
RESULT.md correctly records failure. It contains the pre-fix SDK65ed binary,
resources, mutable project/cache and historical manifests. Do not reuse them
or copy their provenance as if it described the new candidate.

The new location should be exactly `/workspace/outliner-build/startup-native-2`.
Preparation must wait for SDK10 packaging and the matching Studio15 host build
to complete. Those are human ledger labels, not package identities: read and
record the actual final SDK package build/source IDs and executable hash.
No such final candidate identity is asserted by this plan.

After the builds complete, prepare once, without launching any executable:

```sh
set -eu
umask 077
startup_root=/workspace/outliner-build/startup-native-2
studio_build=/workspace/outliner-build/studio-native
studio_source=/workspace/chibi-studio-outliner
test ! -e "$startup_root"
mkdir -m 700 "$startup_root"
mkdir -p "$startup_root/runtime/bin" "$startup_root/runtime/share" \
  "$startup_root/provenance" \
  "$startup_root/prefs" "$startup_root/cache" "$startup_root/tmp" \
  "$startup_root/xdg" "$startup_root/config" "$startup_root/data"
cp -a --reflink=auto "$studio_build/bin/chibi-dev" "$startup_root/runtime/bin/"
cp -a --reflink=auto "$studio_build/share/chibi" "$startup_root/runtime/share/"
cp -a --reflink=auto "$studio_source/tests/acceptance/project" "$startup_root/project"
```

Keep the binary adjacent to the copied share/chibi tree. Copy the fresh original
three-file acceptance project, not the previously opened copy. Do not format
or migrate the source project/resources; if startup later transforms copies,
the originals remain safe. Verify copied trees have no symlinks and freshly
computed project/resource manifests match their corresponding source trees.
Do not hardcode the prior 394-file resource count; record the current count.

### Runner reuse and required sanitizer review

The existing runner SHA-256 is
`11e5d01ab59b5a80e39a9aa1d102a3290dfa2bc1bf570268c09a3262c544a757`.
The parent's final instruction requires the original application log guards
unchanged, with a separate sanitizer review. Accordingly the copied runner
changes only `startup_root` to `/workspace/outliner-build/startup-native-2`.
The initial read-only recommendation to extend its log predicate was not
applied. The following required post-run log inspection shows the complete
application/sanitizer failure criteria; it is not an extra Studio execution:

```sh
if rg -a -n -F \
  -e 'Studio UI:' -e 'runtime frame failed' -e 'Could not initialize' \
  -e 'runtime error:' -e 'AddressSanitizer' \
  -e 'UndefinedBehaviorSanitizer' -e 'LeakSanitizer' "$log"; then
    printf '%s\n' 'Studio capture failed: prohibited application or sanitizer log marker' >&2
    exit 1
else
    match_status=$?
    if [ "$match_status" -ne 1 ]; then
        printf '%s\n' 'Studio capture failed: log could not be checked' >&2
        exit 1
    fi
fi
```

This explicit review prevents a zero-exit UBSan report from being called a
pass; it suppresses nothing and adds no application workload. Independent
read-only reviewer `/root/outliner_runtime_authority` identified the original
wrapper's limited boolean result and agreed these markers need inspection.
The full new runner still needs a source/hash check after preparation. Do not
change ASAN/UBSAN/LSAN options: keep them unset/default as the original runner
does. Preserve `HOME` and `CODEX_HOME`. Keep prefs, XDG data/config/runtime,
Mesa cache and TMPDIR inside the fresh directory with 0700 scratch/runtime
permissions. Continue sourcing `/workspace/outliner-build/test-env.sh` and
asserting the same saved LD_LIBRARY_PATH and lvp ICD; no renderer fallback,
driver probe, display-server launch, security change or additional frames.

Reserve exactly this invocation for the parent after source review/admission:

```sh
bash /workspace/outliner-build/startup-native-2/run-startup.sh \
  > /workspace/outliner-build/startup-native-2/invocation.log 2>&1
```

The new wrapper must refuse existing capture/log files; invoke it once with
noclobber also protecting invocation.log. Its sole application command inside
the existing production `test_policy.run_commands(..., seconds=60)` remains:

```sh
/workspace/outliner-build/startup-native-2/runtime/bin/chibi-dev \
  --project-root /workspace/outliner-build/startup-native-2/project \
  --headless --frames 2 --fixed-dt 0.016666666666666666 \
  --screenshot /workspace/outliner-build/startup-native-2/startup.png
```

Initialization, two frames, rendering/readback and teardown share that one
unchanged 60-second process-group/descendant supervisor deadline. No checkpoint
means the native 1920x1080 canvas. The existing internal readback bound remains
10 seconds. This documented startup route remains separate from the external
acceptance-selector binary-size restriction. Keep SDL offscreen, dummy audio
and the verified Mesa llvmpipe software stack; timing is not hardware GPU
performance or an interaction-latency measurement.

### Fresh provenance and post-run gates

Before the reserved launch, record freshly rather than copy prior values:

- The built and copied executable SHA-256/size, and byte equality; Studio15
  build log, CMake cache, compile commands, source HEAD/status/diff and complete
  tracked+untracked nonignored source manifest (including new files).
- Installed SDK10 package.json, engine.json, actual package/engine build IDs,
  source revision/dirty status/source snapshot, SDK build log, configured
  package directory and Studio SDK compile identity. All must identify the
  same installed package; retain ASan+UBSan/frame-pointer compile evidence.
- Fresh source/copied resource manifests, source/copied three-file project
  manifests, no-symlink checks, current resource count, and exact bundled
  Outliner behavior/library identities matching the unchanged source hashes.
- The runner, unchanged production supervisor, saved environment/profile,
  loader/ICD/software-driver identities and expected environment strings.
  Preserve truthful existing llvmpipe enumeration evidence without claiming a
  new renderer probe or rendered frame during preparation.
- The reviewed Cute offsetof and empty-uniform-compare patch identities and
  their inclusion in the new SDK source/package provenance; the prior concrete
  sanitizer failure is the reason for this affected rerun.

After one launch, require status 0 and no application/sanitizer marker, decode
the newly created PNG, verify 1920x1080 dimensions, inspect actual Studio panels,
text and icons, record its hash and supervisor time, and confirm child cleanup.
Only then describe startup/readback as passing. The empty Start scene exercises
no Outliner interaction, scale or browser behavior. On any failure preserve the
new scratch and diagnostics and stop without automatic retry or suppression.
