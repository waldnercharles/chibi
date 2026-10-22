# Native migration boundaries

The destination is a Chibi implementation of all first-party compiler, runtime,
engine, reflection, tools, build and SDK logic. Third-party C/C++ libraries remain
usable through interop. Existing C hosts, generated first-party parsers, C++ and
Objective-C shims, and CMake descriptions are migration debt, not permanent
exceptions. Shader and platform interfaces need an explicit supported lowering
or interop boundary; moving a file into a vendor directory does not make it a
third-party dependency.

## First native slice

Reuse the authored compiler's source analysis and semantic instructions, then
lower a deliberately bounded scalar subset to LLVM IR. LLVM must validate that
IR and produce a native object which links and executes without a generated C
translation unit or a first-party C helper. The first proof can use external LLVM
tools; LLVM's C API through Chibi interop is a subsequent integration option, not
a reason to retain a first-party C adapter. SLJIT remains available while its
replacement has not demonstrated parity.

The first scalar slice is not the runtime migration. Its acceptance checks are:

1. Authored source takes the shared compiler frontend, not a second expression
   parser or a manually constructed instruction table in place of that path.
2. Declared supported operations match bytecode and SLJIT, including integer
   wrapping, checked division and shifts, branch selection and failure behavior.
3. Unsupported types, nominal/container/reference representations and operations
   reject before publishing partial IR. A matching primitive storage tag alone
   is not enough to admit a managed value.
4. LLVM verifies the emitted module, emits the object and the linked executable
   produces the expected result. Record source, host, compiler, LLVM and target
   identities; compare native bytes only for the same target and toolchain.
5. The real cross-package native ABI consumer passes, not just same-package
   unit checks. Target data layout comes from LLVM; guessed C-runtime offsets
   are not a native ABI contract.

The next native island adds an explicit foreign-pointer/size ABI, allocation,
load/store, an output call and release with failure/ownership tests. Raw foreign
pointers are distinct from managed-pointer semantics. Do not change a managed
pointer representation in a backend branch without the semantic ABI owner's
review.

## Parallel work and temporary interfaces

An authored parser can replace the raw syntax dependency only after it reproduces
the required facts, locations, links and diagnostics over the actual grammar.
A package/import/literal subset must reject unsupported input atomically and
remain opt-in until those gates pass. Lexical coverage alone is not parser parity.

Native ABI helpers should expose only types with a current lowering consumer.
Bootstrap selection and manifest checks should be pure functions over decoded
data, separate from I/O. Do not add competing JSON, path, collection or string
libraries to get around an unfinished standard library. Prefer small internal
helpers and refactor their consumers together when a shared facility becomes
available. New files in a recorded compiler package also require the bootstrap
manifest/source-closure check, even when no prepared-ABI version changes.

## Bootstrap and artifacts

The default should select the latest build, with an explicit version or local
executable/package override. The recommended implementation selects the latest
successful validated build compatible with the requested platform and ABI,
resolves it once, verifies its bytes and records the immutable generation,
source revision, executable digest and resources. An explicit invalid override
fails; it must not silently fall back to a moving channel. Reproduction uses the
recorded identity instead of resolving latest again.

A compiler-host executable and its `.chibip` payload are not automatically a
complete build/SDK package. Promote a generation only after all its declared
platform artifacts, resources, source/provenance and license notices are
available and tested together. Local overrides remain explicit and their exact
bytes are recorded; they do not become validated channel releases merely by
being selected.

## Retirement gates

Keep the existing host and test oracles until the corresponding Chibi runtime
supports the closure's values, memory, execution, text/arrays, syntax, I/O and
failure contracts. Compiler self-reproduction on a C runtime is useful evidence,
not proof that the runtime, engine or SDK is Chibi-native. Native compiler
self-reproduction requires the new native compiler to produce its next stage
and reproduce the supported output on the same target.

Retire each C/CMake path only with an identified Chibi replacement, independent
review, preserved assertions and clean-checkout plus installed/relocated-package
tests for that path. Preserve required source, notices and relinking obligations
when moving implementation between GPL tool and LGPL reusable packages. The
final build graph must not need a first-party C or CMake fallback. Ordinary CI
should exercise supported semantic/native targets and recovery; artifact
publication should promote exact tested generations. Third-party rebuilding is
a separate dependency operation, not an ordinary first-party build requirement.
