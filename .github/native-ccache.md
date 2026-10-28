# Native CI compiler-object cache

Only the Linux and macOS native jobs opt in. Install ccache through the existing
signed Ubuntu dependency helper or Homebrew; do not change compiler versions.
CMake and Graph already discover its compiler launcher. No build tree, linked
program, test result, package, checkout, credential or authored-asset compiler
result is restored. Every existing build and test still runs.

`.github/native-ccache.py` creates a job-local configuration outside the cached
store. `compiler_check = content` detects replaced compiler binaries, and the
existing default ccache input/argument/header checks remain enabled (no
sloppiness or disabled directory hashing). Both `hash_dir = true` and empty
`sloppiness` are explicit in the job-local config. The store is limited to 500 MB
per job. Debug ASan/UBSan, unsanitized bootstrap and Release compilation can
share this store safely: compiler arguments, including sanitizer flags, are part
of ccache's individual object keys. Restoring a snapshot never substitutes a
linked program or another build profile's object without ccache validation.

Snapshots use a nested key:
`ccache-v2-OS-ARCH-TOOLCHAIN-RECIPE-SHA`. The toolchain hash includes OS,
architecture, runner image/version, C/C++ compiler versions **and binary
contents**, ccache version, installed LLVM toolchain package revisions (including
LLVM shared-library companions), and macOS SDK path/version/build. Linux queries
the installed clang/libclang/libllvm/llvm package families; macOS queries LLVM
and its installed Homebrew dependencies. A missing or
unreadable compiler, or failed SDK/package identity command, fails before restore.
The recipe hash additionally includes the existing profile policy, Makefile,
CMakeLists.txt, CMake modules, Graph toolchain sources, pinned build-tool and
third-party archive manifests, and this setup script.

Restore first tries the exact SHA key, then the same recipe prefix, then the
same toolchain prefix. The last tier allows harmless build-system edits to
reuse still-valid compiler objects rather than discard the entire snapshot.
No fallback crosses a toolchain identity. GitHub picks the most recent matching
snapshot within each prefix; ccache then checks source, compiler arguments and
included headers for every compilation as usual. A prefix match itself is not
proof that an object is usable. The full recipe/SHA key remains the only save
key, preserving provenance without a second cache or broader writes. An exact
hit needs no replacement. The v2 namespace starts cold once; v1 snapshots are
not restored through a broad legacy fallback.

Restore is allowed for PRs. Save requires a successful job on a `push` to
`refs/heads/main`; PRs and manual dispatches do not save. GitHub additionally
isolates PR caches under their merge ref, so main cannot restore them even if a
PR changes its own workflow. This workflow never uses `pull_request_target`.
Cache contents are public, not an authenticity boundary: do not add sensitive
paths or add fallback outside the toolchain prefix. The configuration file is not cached.

The official split `actions/cache/restore@v4` and `actions/cache/save@v4` actions
use the supported v2 cache backend and follow this repository's major-version
pinning style for official actions. Cache misses proceed with normal compilation;
cache-service warnings do not replace or skip build/test gates. No workflow
permissions, timeouts, retries or test parallelism change.

## Evidence and rollout

`python3 .github/test-native-ccache.py` checks primary/compatibility invalidation, compiler-content identity,
config location, output provenance and workflow save/restore wiring offline. These tests mock tool identity;
they do not demonstrate cache hits or build speed. The native jobs log effective
configuration, zero counters after restore, and report verbose hit/miss/cache-size
statistics even after a failed build. A canceled runner may not emit final stats.

Hosted cold/warm evidence is **pending**. A PR cannot seed main's cache. After
review and integration, a successful normal main push seeds each platform; a
subsequent main push or PR with compatible identity can establish warm reuse.
Record runner image/compiler identity, restore key/status and duration, compiler
cache hit/miss counts and size, build/Graph/Release durations, save duration, and
all test results for both runs. Include restore/save overhead in the comparison.
Changes of runner image or toolchain deliberately start cold. A 500 MB store may
evict objects; measure before increasing it. Do not claim an end-to-end speedup
from cache hits alone: linking, authored Graph asset compilation and tests are
not cached, and the baseline macOS Graph time includes non-cacheable work.
No extra benchmark dispatch or reduced coverage is part of this change.

References:
- [Official cache action and supported backend](https://github.com/actions/cache/tree/v4)
- [Separate restore/save actions](https://github.com/actions/cache/blob/v4/caching-strategies.md)
- [GitHub cache scope and security](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching)
- [ccache configuration and hashed inputs](https://ccache.dev/manual/latest.html)

## Let trusted main snapshots finish

CI keeps its existing concurrency group, including workflow, ref, event and
fast/sanitizer profile. An incoming push to `main` does not cancel the active
run in that group: it can complete tests and reach the unchanged success-only
native cache saves. The default single pending slot retains the newest queued
run, replacing older pending work; this does not run every main push in parallel
or introduce a long queue. No paid capacity or runner count is added.

PR runs still cancel superseded runs. Manual and scheduled runs retain their
previous cancellation behavior in their separate event/profile groups, so they
do not interrupt the main-push group. A failed or timed-out main run still cannot
save. Completing an older active main run can cost its remaining runner minutes
and delay validation of the newest main commit; the benefit is avoiding repeated
partial cold builds whose cancellation prevents publishing reusable objects.
This is not a measured cost or speed claim.

The offline native-profile fixture checks the expression's event/ref truth table
and preserves group isolation. GitHub owns pending-slot scheduling; no local
fixture claims to execute the service queue. See [official concurrency semantics](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency).
