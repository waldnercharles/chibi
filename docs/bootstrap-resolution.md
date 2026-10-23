# Bootstrap selection and package contract

This additive slice implements pure policy for the all-first-party-Chibi
migration. It does **not** change the current build default,
`tools/compiler/runtime.json`, SDK installation, release publication, or any
workflow. The runtime manifest pins compiler-only hosts; it is not a complete
cold-build package. No new production Python, shell, C or CMake is introduced.

## Selection policy

`tools/toolchain/bootstrap/resolver/Resolver.chibi` owns selection. The first
present input wins: explicit invocation selection, captured environment
selection, then `latest`. Presence differs from an empty string. Invalid or
unavailable explicit input fails; no later source is substituted.

The internal encoding is `latest`, `version:GENERATION`, or `local:PACKAGE`.
These are not new public CLI flags or environment variables. Public wiring and
names remain with the build/SDK owner. Version identifiers are bounded ASCII
path components; the publisher must make their content immutable. Local paths
retain their spelling, including spaces and Windows separators, for the target
platform to resolve. An executable override must locate its matching package
resources; a compiler-runtime executable alone is insufficient.

## Internal decoded manifest

`Manifest.chibi` consumes typed values, not JSON text. This avoids adding a
parallel generic parser while that shared capability is unsettled. The eventual
adapter must reject unknown/duplicate keys, preserve integer precision, bound
input size, and compute the digest of the exact trusted manifest bytes. The
private schema and serialized representation remain provisional until that
adapter and publication contract are reviewed together.

- `Generation`: format `1`, kind `chibi-toolchain`, immutable generation ID,
  repository `waldnercharles/chibi`, source commit/tree SHA-1, validation status.
- `Platform`: one of `linux-x64`, `macos-arm64`, `windows-x64`; unique archive
  filename, positive byte count and lowercase SHA-256; executable path; native
  ABI, prepared format and bootstrap version; contiguous file-inventory span.
- `File`: canonical relative regular-file path, positive byte count, lowercase
  SHA-256 and executable status. An inventory has at most 4,096 files per
  platform and no duplicate, absolute, parent-relative or backslash paths.
- `Manifest.Contents` carries the generation, platform array and file array.
  Scalar records with union array payloads match the current compiler's record
  support; these are not proposed public standard-library types.

Every listed platform is checked before one is selected. Inventory spans cover
all files exactly once. Each platform must include its executable and the
current installed resource entry points:

```text
share/chibi/compiler/bootstrap.kdl
share/chibi/compiler/bootstrap/bootstrap.json
share/chibi/compiler/bootstrap/compiler.chibip
share/chibi/compiler/package_compiler/Compile.chibi
share/chibi/core/strings/Strings.chibi
```

These entry points alone do not prove compiler completeness. The existing
bootstrap loader must verify the transitive source closure and resource
consistency. The executable must also provide the required build services;
ABI-compatible compiler-only hosts do not qualify. Listing a platform here does
not establish its implemented build-host support or execution coverage.

## Resolve once and verify before use

The coordinator obtains the dedicated validated-toolchain channel once per
invocation/CI run and shares the exact manifest and digest with all platform
jobs. Do not use GitHub's unqualified latest-release API: this repository also
publishes unrelated release kinds. A version selection reads only its exact
generation. A local selection reads only the specified package. No failed
selection silently falls back to latest or another platform.

`Resolve` checks the whole manifest, requested generation, publication state and
consumer ABI requirements. Its owned `Resolved(snapshot, platform, files)`
result captures the generation/source/manifest identity, local provenance, and
selected executable/resource inventory. Downstream work consumes that result;
none of these functions can refetch or upgrade a channel.

The I/O adapter must enforce these stages in order:

1. Authenticate the manifest source and compute its actual SHA-256. Use
   `Resolve` once with the immutable decoded generation and that digest.
2. For an archive, measure/hash actual bytes and call `DownloadError` **before**
   extracting or executing anything. Extract into a private directory, rejecting
   links, path escapes and target-filesystem name collisions. An unpacked local
   package may omit the complete archive descriptor; published packages may not.
3. Hash every actual regular file and call `InventoryError` with the resolved
   expected inventory. Missing, extra, changed or non-executable files fail.
   Keep the verified files isolated from mutation until execution; mere path
   comparison is not filesystem containment or protection against races.
4. Inspect the verified executable's actual platform/ABI/bootstrap capability,
   required build services and loader-verified compiler completeness; pass those
   observations to `RuntimeError`, not copies of manifest claims.
5. Record the snapshot, observations and selected file hashes in build evidence.
   Local selection always records `development=true`: its metadata cannot
   self-assert trusted promotion. Reproduce a promoted seed via its authenticated
   version selection instead. Hash a local package's
   actual files to produce its inventory; never synthesize archive hashes.

These functions compare observations; they do not perform downloads, calculate
hashes, safely extract archives, inspect executables or provide a security
sandbox. Calling only `Resolve` is not verification of an actual payload.

Cache by content hash. Offline use requires an explicitly recorded cached
generation and must not claim that an unreachable channel was checked. The
candidate is built with the prior resolved seed. Later self-build stages execute
their newly built candidate. Promotion advances latest only after that exact
candidate and its next generation pass; never bootstrap a candidate from its own
not-yet-validated publication.

## Authored fixture and remaining integration

`test/tools/bootstrap/ResolverTests.chibi` is a pure package. Its public `Tests`,
`SelectionTests`, `ManifestTests` and `ResolutionTests` entries return arrays of
failure descriptions; an empty array passes. They require only `core:strings`
and the ordinary pure compiler catalog, not process, filesystem, environment,
network or build-host natives. Import the resolver with the `bootstrap`
collection rooted at `tools/toolchain/bootstrap`.

The fixture covers precedence/explicit emptiness, path preservation, malformed
selection, wrong release kind/source identity, incomplete and duplicate platform
or file entries, inventory spans, archive/resource hashes and sizes, missing
resources/executable modes/build services, ABI/format mismatch, exact-version
failure, local development and unpacked local packages. Fixture values are
synthetic; they are not downloadable production artifacts.

The bounded fixture has been compiled and run on the checksum-pinned Linux
runtime host using the existing compiler closure. Execution evidence is recorded
in the checkpoint report; full repository suites, macOS/Windows execution,
network transport, installed SDK relocation and publication are separate gates.
The fixture is not yet connected to the repository test dispatcher. Decoder,
I/O adapter, production package creation, CI resolve-once wiring and channel
promotion remain unimplemented; the existing compiler runtime pin stays intact.

This private module reuses `core:strings` rather than adding another text
implementation. General helpers move to a shared package only with a real
second consumer; selection and release policy stay here. Consolidate the
provisional descriptor representation with the shared manifest parser when the
I/O/packaging consumer lands, without introducing a second long-lived API.
