# Strict compiler-host construction: retained initializer failure

The fresh 17-target, 88-C-unit, 16-archive host recipe failed while compiling
`src/runtime/text_definition.c` with the existing GCC 14 GNU23/O3/strict-warning
policy. No flags were weakened. The Graph command took 17.880250 seconds;
the bounded phase took 20.235690 seconds and exited 1 within its original
180-second cap. This early failure is not a complete-build timing result.

There were exactly 19 fatal `initializer element is not constant
[-Werror=pedantic]` diagnostics: 17 expansions from `text_definition.h:29`,
one from line 61 and one from line 72. `text_definition.c` expands the operation
rows into static const parameter arrays; `capabilities/definition.h` inserts
the supplied `pin_value` compound-literal expression as `.default_value`.

The earlier cookie/equality diagnostics did not recur in their successful
units. Exactly 31 object/depfile/Graph-record triples were produced. The failed
unit left a depfile without an object or record. There were no archives, host,
host inspection or produced-host execution. All 31 genuine records matched;
2,236 dependency occurrences over 176 paths passed separate full-width identity
and SHA-256 checks. A postfailure independent read verified the original
4,073-source-file and tool/provenance closures. It does not convert failure
into a pass or replace success-only in-budget checks that were never reached.

Process cleanup reached authoritative ECHILD with no surviving children,
signals or errors. The generic wrapper failure text did not indicate a cleanup
failure: the root command itself exited 1. The original deadline was respected.

Retained evidence:
- Plan SHA-256: `fff3c4cc8e3179aec72fc1f1d943db4b9a534cb9ce2aa547260de81eb7564406`.
- Controller SHA-256: `a5f5f9d27e52c875006be608a0c285b607d14185338bf126c689ed5c1a8e92a1`.
- Failure-review SHA-256: `b4d2a5f7862fa68f716d82ccad41ab924cacf6c7040dc57e1c047719638f9667`.
- Full local archive: `chibi-compiler-host-initializer-failure-20261104.tar.gz`,
  47,255,956 bytes, SHA-256 `0eb6e09dbb87a85abc0d69c0f4a157a38479bbd944ad399725df63f4882ae39e`.

Archive member bytes, modes, link and unchanged original identities were read
back. The archive is retained locally, not uploaded as a release. A potential
source correction was not applied or tested in this retained failed packet.
A later [focused initializer correction](NATIVE-DEFAULT-INITIALIZERS.md) passes
strict compilation and a separate standalone descriptor runtime proof; it does
not retroactively change this failed result.
