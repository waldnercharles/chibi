# Manual bootstrap maintenance

Routine validation lives in the single `.github/workflows/ci.yml`. Preparing
replacement bootstrap inputs is **not** routine CI. The scripts below replace
four retired workflows without a release job, token with write permissions,
automatic pin commit, or dispatch. They only prepare local files. Nothing here
authorizes a seed refresh or publication; obtain explicit approval for those
operations and respect current holds before running them.

Run from a clean checkout of the exact reviewed commit. Use a **new directory
outside the checkout** for each output. Keep each output and its logs when a
command fails; do not publish partial results. Dependencies must already be
installed. The existing CI watchdog caps the whole preparation command at the
old host (30 minutes), runtime (45 minutes), native (180 minutes), and source
bundle (5 minutes) limits. Runtime comparison and packaging share a hard five-minute cap; the old
ten-minute publication job no longer exists. The scripts do not extend compiler fixed-point
iterations or any compiler-internal deadline.

## Tools and platforms

All commands need Git, Bash, Node, `shasum`, the GitHub CLI for **read-only** pinned
asset downloads, and the dependencies used by the build graph. The runtime
proof additionally needs PowerShell 7 and `readelf` (Linux) or `otool` (macOS).
Provision a Linux x64 or macOS arm64 machine before preparing binaries:

- **Ubuntu 24.04:** install `ccache clang-18 clang-tools-18 libclang-18-dev
  libclang-rt-18-dev llvm-18 lld-18 libgl1-mesa-dev libvulkan1 mesa-vulkan-drivers
  libx11-dev libxcursor-dev libxext-dev libxi-dev libxrandr-dev libxss-dev
  libxtst-dev libwayland-dev libxkbcommon-dev libasound2-dev libpulse-dev`.
  Export `CC=clang-18 CXX=clang++-18 LIBCLANG_ROOT=/usr/lib/llvm-18
  LLVM_BIN=/usr/lib/llvm-18/bin`.
- **macOS 15 arm64:** install Homebrew `llvm lld`; set
  `LIBCLANG_ROOT=$(brew --prefix llvm)`, `CC=$LIBCLANG_ROOT/bin/clang`, and
  `CXX=$LIBCLANG_ROOT/bin/clang++`. For native seeds, create a new tools directory
  and link `opt` and `llc` from LLVM plus `ld.lld` and `ld64.lld` from
  `$(brew --prefix lld)/bin` into it; export its absolute path as `LLVM_BIN`.
  Put `$LIBCLANG_ROOT/bin` on `PATH`.

Provisioning is separate from the bounded preparation command; it never gives a
compiler phase extra time or a retry after that command's deadline. No script
installs software, changes access, or creates a release on its own.

## First build host

```sh
bash .github/maintenance/bootstrap-host.sh /absolute/new/host-linux-x64
# Run separately on macOS, with a different output directory.
```

This retains `tools/build.sh build --release --target chibi-build-host`, checks
`--protocol-version` is exactly **15**, and saves `chibi-build-host-PLATFORM`,
`SHA256SUMS`, `build.log`, source commit/tree/platform evidence and the proposed
`hosts.tsv` row. It consumes the existing pinned build host exactly as before.
Prepare both platforms from the same clean commit before considering publication.
The tag convention remains `bootstrap-host-COMMIT`. Update
`tools/bootstrap/hosts.tsv` only in a separately reviewed change after verifying
the published binaries.

## Native compiler seed

```sh
bash .github/maintenance/native-seed.sh /absolute/new/native-linux-x64
# Run separately on macOS, with a different output directory.
```

This uses the matching row of `tools/bootstrap/native-seed.tsv` when one exists
and otherwise keeps the bytecode-seeded route. It runs `native-self-host` with
`--jobs 4 --skip-vm`, retaining the native build's stage-two/stage-three equality
and the existing stage-three/stage-four fallback for an older seed. Only the
executable named by the successful fixed-point log is retained as
`chibi-native-PLATFORM`. The original log, source identity, checksum and proposed
TSV row are kept. No retry changes the fixed-point bound. The tag convention
remains `native-seed-COMMIT`; publication/pinning is separate from preparation.

## Compiler runtime seed and corresponding source

On each of Linux x64 and macOS arm64, using the **same clean commit**:

```sh
bash .github/maintenance/runtime-seed.sh -OutputDirectory /absolute/new/runtime-linux-x64
```

The proof fetches the pinned bytecode seed, builds `chibi-compiler-host`, then
copies only that runtime, `tools/compiler` and `packages/core` into the output
directory. It rejects native source files, checks process and OS architecture,
and launches the copied runtime with its environment cleared, empty `PATH`,
unavailable `CC`/`CXX`, and temporary directories pointing into the proof. The
runtime's normal bootstrap recovery/fixed-point rules are unchanged. This is
environment/input isolation, not a filesystem sandbox.

Keep the entire proof directory. It contains recovered `compiler.chibip` and
its hash, the platform runtime and its hash, stdout/stderr, native imports, and
JSON recording commit/tree, platform, runtime/closure hashes, input inventory,
cleared child environment, exit status and build/recovery timings.

Copy the two proof directories to one clean checkout at that same commit. Then:

```sh
bash .github/maintenance/package-runtime.sh \
  /absolute/runtime-linux-x64 /absolute/runtime-macos-arm64 \
  /absolute/new/runtime-release https://example.invalid/replace-with-real-evidence-location
```

Supply the actual durable URL where the reviewed preparation evidence will be
retained, not the example URL. The existing manifest field `run_url` records
that URL; a manual preparation is not represented as a GitHub Actions run.
Packaging rehashes the actual runtime and compiler bytes, validates both proofs,
requires cross-platform compiler equality, and preserves the Windows entry of
format-2 `tools/compiler/runtime.json` unchanged. The package contains:

- Linux/macOS runtime assets and a proposed `runtime.json`, with exact commit,
  tree, size/hash and `COMPILER_HOST_MAX_ARTIFACT_BYTES`;
- each platform's recovery evidence, imports and stdout/stderr;
- the exact commit's `git archive` (including the SLJIT patch and build inputs),
  the pinned upstream SLJIT and Tree-sitter source archives, and Chibi/fmt/yyjson/
  SLJIT/Tree-sitter notices;
- `SHA256SUMS` covering every prepared release file.

The tag remains `compiler-runtime-preparedVERSION-COMMIT`. Review corresponding
source and notices using [distribution licensing](distribution-licensing.md);
a successful preparation is not a certification of distribution compliance.

## Optional source transfer

```sh
bash .github/maintenance/source-bundle.sh /absolute/new/source-transfer
```

No current external consumer was verified; this preserves the cheap on-demand
capability without automatic packaging or artifact upload. A full-history
checkout is required. The bundle contains only HEAD and its reachable history
under `refs/heads/source-bundle`, not unrelated tips or local configuration.
It is verified in a fresh bare repository, cloned again, checked against the
exact commit/tree, and passed through `git fsck --full`. `manifest.json` and
`SHA256SUMS` identify the output. Uncommitted work is not included. No checkout
ref is modified. Transfer the output only when its destination is authorized.

## Separately authorized publication recipe

These are manual review gates, **not commands run by CI or the preparation
scripts**. The retired runtime workflow's automatic branch pin and follow-on
dispatch are deliberately gone.

1. Obtain explicit approval for the exact source commit, platform assets, tag,
   repository, corresponding-source material and pin change. Require successful
   proofs on both runtime platforms. Do not substitute a successful host/native
   build for the runtime isolation/equality proofs.
2. Assemble a new flat release directory. For host/native preparation, copy only
   the intended `chibi-*-PLATFORM` binaries, their source evidence/logs and
   required source/notices; do **not** publish `previous/` or the complete proof
   input trees. For a runtime release, use the complete package above. Verify
   every hash locally. Review that all files are intended public material.
3. Verify the tag does not already identify a published release. Never replace
   published assets. An API/authentication/rate-limit error is not evidence of
   absence. If a draft exists, inspect its target and complete asset inventory;
   resume only that exact approved commit, and do not overwrite unknown assets.
4. With separately authorized credentials, use `gh release create TAG --draft
   --prerelease --latest=false --target COMMIT --repo REPOSITORY` with reviewed
   title/notes, then `gh release upload TAG FILE... --repo REPOSITORY` for the
   explicit reviewed inventory (no `--clobber`). Compare the draft's asset list
   exactly with the expected inventory before publishing it via
   `gh release edit TAG --draft=false --prerelease --latest=false --repo
   REPOSITORY`.
5. Download the published assets to a **new** directory. Verify the tag resolves
   to the exact commit, the release is nondraft/prerelease, the asset-name set is
   exact, and all bytes match both `SHA256SUMS` and the prepared files. For a
   runtime, compare the published `runtime.json`, evidence, imports, source and
   notices byte-for-byte with the reviewed package; this also retains both
   platform checks and the unchanged Windows pin. Stop on any mismatch.
6. Only then propose the verified TSV rows or `runtime.json` in a reviewed PR.
   Do not commit/push to a branch that advanced during preparation. Do not
   dispatch CI from these scripts. Use the single CI workflow through its normal
   approved entry points; never revive the removed workflows.

## Retirement map

| Retired workflow | Preserved capability | New owner/trigger |
| --- | --- | --- |
| `bootstrap-host.yml` | Linux/macOS protocol-15 host, hashes and pins | `bootstrap-host.sh`, explicit local preparation |
| `compiler-runtime-seed.yml` | Runtime build, clean-room recovery, equality, import/provenance/source/notices material | `runtime-seed.sh` + `runtime-seed.ps1`, then `package-runtime.sh` + `package-runtime.js`, explicit local preparation |
| `native-seed.yml` | Native seeded fixed point, logs, hashes and pins | `native-seed.sh`, explicit local preparation |
| `source-bundle.yml` | Self-contained exact-source history and verification | `source-bundle.sh`, optional local transfer |

All scripts are cold maintenance paths; none is an automatic release route.
