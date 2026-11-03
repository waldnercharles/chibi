# Manual bootstrap maintenance

Routine validation lives in the single `.github/workflows/ci.yml`. Preparing
replacement bootstrap inputs is **not** routine CI. The scripts below replace
the retired workflows without a release job, token with write permissions,
automatic pin commit, or dispatch. They only prepare local files. Nothing here
authorizes a seed refresh or publication; obtain explicit approval for those
operations and respect current holds before running them.

Run from a clean checkout of the exact reviewed commit. Use a **new directory
outside the checkout** for each output. Keep each output and its logs when a
command fails; do not publish partial results. Dependencies must already be
installed. Native preparation keeps its existing owned-command deadline and
fixed-point bound. A failed command retains its output for inspection.

## Tools and platforms

Shell preparation needs Git, Bash, Node, `shasum`, LLVM/LLD 22 or newer and the
GitHub CLI for checksum-pinned asset downloads. Set `LLVM_BIN` and
`CHIBI_LLVM_BIN` to the same tools directory. `.github/native-llvm.sh` records
CI provisioning. No C compiler or libclang is needed to prepare native seeds.

Windows artifact preparation needs PowerShell 7, Git and the GitHub CLI. It
restores an existing completed CI proof and does not run a compiler. The normal
Windows checkout entry supports Windows PowerShell 5.1 and runs the native CLI.

## Native compiler seed

```sh
bash .github/maintenance/native-seed.sh /absolute/new/native-linux-x64
# Run separately on macOS, with a different output directory.
```

This uses the matching row of `tools/bootstrap/native-seed.tsv`. The native build
entry also requires its platform's verified seed. It runs `native-self-host` with
`--jobs 4`, retaining the native build's stage-two/stage-three equality
and the existing stage-three/stage-four fallback for an older seed. Only the
executable named by the successful fixed-point log is retained as
`chibi-native-PLATFORM`. The original log, source identity, checksum and proposed
TSV row are kept. No retry changes the fixed-point bound. The tag convention
remains `native-seed-COMMIT`; publication/pinning is separate from preparation.

For Windows, first require the exact reviewed commit's `Native self-host
(Windows)` CI job to pass its fixed-point and runtime checks. From a clean
checkout of that commit, prepare the artifact on any PowerShell 7 host:

```powershell
# Set $run to the accepted CI run ID. The new output must be outside the checkout.
./.github/maintenance/native-seed.ps1 -RunId $run -OutputDirectory C:/proof/native-windows-x64
```

This verifies the repository, `ci.yml`, source commit, completed run, successful
Windows job and unique unexpired artifact. It checks the downloaded archive
against GitHub's SHA-256 digest before restoring selected stage files. Actual
compiler bytes must match the producer log and its bounded stage-three/four
or stage-four/five equality. The script retains the artifact, hashes, job-step
evidence and a proposed `windows-x64` TSV row for
`chibi-native-windows-x64.exe`. It also archives the exact compiler/core closure,
restores it and compares every tracked file's hash, then rechecks the clean
source commit/tree. Owned child commands share the existing 180-minute native
preparation budget. Failed preparation keeps its output for inspection.

The Windows row remains absent until the accepted binary is separately
published and downloaded again under the publication procedure below. Normal
`tools/build.ps1` selection fails clearly without that row; `--native-driver`
can select a candidate for validation without changing the manifest.

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

## Separately authorized native seed publication

Preparation does not authorize publication or a pin change. The former C-host
and prepared-bytecode runtime release procedures are retired with their
consumers; they are not bootstrap alternatives.

1. Obtain approval for the exact source commit, platform assets, tag,
   repository, corresponding-source material and pin changes. Require each
   proposed platform's fixed-point and owning runtime checks to pass.
2. Assemble a new release directory containing only the reviewed
   `chibi-native-PLATFORM` executable (`.exe` for Windows), its preparation
   evidence, checksums, corresponding source and required notices. Omit private
   scratch and input trees. Check every file against the prepared inventory.
3. Verify that the tag does not already identify a published release. An API,
   authentication or rate-limit error does not establish absence. If a draft
   exists, verify its target and complete asset inventory before resuming it.
   Never replace unknown or published assets.
4. With the separately authorized credentials, use
   `gh release create TAG --draft --prerelease --latest=false --target COMMIT --repo REPOSITORY`
   and `gh release upload TAG FILE... --repo REPOSITORY` for the exact reviewed
   inventory, without `--clobber`. Compare the complete draft asset list with
   that inventory before publishing with
   `gh release edit TAG --draft=false --prerelease --latest=false --repo REPOSITORY`.
5. Download the published assets into a new directory. Verify the exact tag
   commit, prerelease state, asset names and every checksum against the reviewed
   preparation. Keep the corresponding source and platform evidence with the
   executable. Stop on any mismatch.
6. Propose only the verified platform rows in `tools/bootstrap/native-seed.tsv`
   through a reviewed PR. Recheck the source branch before committing. These
   scripts do not commit, push or dispatch CI.

## Retirement map

| Retired workflow | Preserved capability | New owner/trigger |
| --- | --- | --- |
| `bootstrap-host.yml` | Default native build entry | Retired with its unused host downloader and pin table |
| `compiler-runtime-seed.yml` | Retired C host and prepared-bytecode recovery | Removed with its runtime/seed consumers |
| `native-seed.yml` | Native seeded fixed point, logs, hashes and pins | `native-seed.sh`, explicit local preparation |
| `source-bundle.yml` | Self-contained exact-source history and verification | `source-bundle.sh`, optional local transfer |

All scripts are cold maintenance paths; none is an automatic release route.
