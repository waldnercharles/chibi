# Native CI compiler-object cache

Only the Linux and macOS native jobs opt in. Install ccache through the existing
signed Ubuntu dependency helper or Homebrew; do not change compiler versions.
CMake and Graph already discover its compiler launcher. No build tree, linked
program, test result, package, checkout, credential or authored-asset compiler
result is restored. Every existing build and test still runs.

`.github/native-ccache.py` creates a job-local configuration outside the cached
store. `compiler_check = content` detects replaced compiler binaries, and the
existing default ccache input/argument/header checks remain enabled (no
sloppiness or disabled directory hashing). The store is limited to 500 MB per
job. Debug ASan/UBSan, unsanitized bootstrap and Release compilation can share
this store safely: compiler arguments, including sanitizer flags, are part of
ccache's individual object keys. The outer profile labels that combination;
Makefile changes also invalidate the compatibility prefix.

The snapshot compatibility hash includes OS, architecture, runner image version,
C/C++ compiler versions, ccache version, macOS SDK path/version, Makefile,
CMakeLists.txt, CMake modules, Graph toolchain sources, the pinned build-tool and
third-party archive manifests, and the setup script. There is no broader fallback
across incompatible prefixes. Source changes are checked by ccache, not excluded
from its hash. The snapshot suffix is the workflow SHA; a new main commit can
restore the latest compatible snapshot and save additional objects under a new
immutable key. An exact hit needs no replacement.

Restore is allowed for PRs. Save requires a successful job on a `push` to
`refs/heads/main`; PRs and manual dispatches do not save. GitHub additionally
isolates PR caches under their merge ref, so main cannot restore them even if a
PR changes its own workflow. This workflow never uses `pull_request_target`.
Cache contents are public, not an authenticity boundary: do not add sensitive
paths or broaden restore prefixes. The configuration file is not cached.

The official split `actions/cache/restore@v4` and `actions/cache/save@v4` actions
use the supported v2 cache backend and follow this repository's major-version
pinning style for official actions. Cache misses proceed with normal compilation;
cache-service warnings do not replace or skip build/test gates. No workflow
permissions, timeouts, retries or test parallelism change.

## Evidence and rollout

`python3 .github/test-native-ccache.py` checks compatibility invalidation, config
location and workflow save/restore wiring offline. These tests mock tool identity;
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
