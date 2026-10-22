# Bootstrap selection and package contract

This is an additive implementation slice of the all-first-party-Chibi migration.
It does **not** change the current build default, `tools/compiler/runtime.json`,
SDK installation, release publication, or any workflow. The existing runtime
manifest pins compiler-only hosts; it is not a complete cold-build package.

## Selection policy

`tools/toolchain/bootstrap/resolver/Resolver.chibi` owns pure selection policy.
The first present input wins: explicit invocation selection, captured environment
selection, then `latest`. Presence is distinct from an empty string. Invalid or
unavailable explicit input fails; no later source is substituted.

The internal encoding is `latest`, `version:GENERATION`, or `local:PACKAGE`.
These are not new public CLI flags or environment variables. Public wiring and
names remain with the build/SDK owner. Version identifiers are single bounded
ASCII path components; the publisher must make their content immutable. Local
paths retain their exact spelling, including spaces and Windows separators, for
the consuming platform to resolve. A local package must include the executable
and matching compiler/core/bootstrap resources, not only a compiler runtime.

## Resolve once, then consume the snapshot

The eventual I/O adapter obtains the dedicated validated-toolchain channel once
per invocation/CI run, validates a complete generation manifest, and passes the
same immutable generation to every platform job. Do not call GitHub's unqualified
latest-release API: the repository publishes unrelated release kinds. A version
selection reads only its exact generation. A local selection reads only the
specified package. Neither may silently fall back to latest.

Downloads must be verified against trusted size and SHA-256 before execution.
The consumer also verifies the selected platform, native/prepared/bootstrap
formats, and the required resources. Local development records its actual
content hashes and explicitly marks unpromoted provenance. Cache identity is
content-addressed; offline use requires an explicitly recorded generation,
never a claim that an unreachable channel was checked.

The candidate is built with the previously resolved seed. Later self-build
stages intentionally execute their new candidate. Promotion advances the channel
only after the exact candidate and next generation pass validation; it must not
bootstrap a candidate from its own not-yet-validated publication.

## Current validation seam

The authored fixture uses only `core:strings` and the build host's report
capability. Run with the existing build host, compiler resources, and core:

```text
chibi-build-host --root ROOT --source test/tools/bootstrap/ResolverTests.chibi \
  --core packages/core --collection bootstrap=tools/toolchain/bootstrap \
  --compiler tools/compiler --compiler-core packages/core \
  --entry Tests --timeout-ms 30000
```

Selection fixtures cover precedence, explicit empty values, path preservation,
malformed overrides, and rejection of a general latest-release URL. Package
manifest validation, I/O/hash/ABI observation, matrix resolve-once wiring and
publication are not yet implemented by this first checkpoint. This fixture is
not wired into the repository dispatcher yet. Passing it is not evidence of
network/download, SDK relocation, cross-platform execution, or full-suite
coverage.

This private module is intentionally not a new public standard-library API.
It reuses `core:strings` rather than adding a second text implementation. General
helpers should move to a shared package only when a real second consumer needs
them; selector and toolchain release policy stay here.
