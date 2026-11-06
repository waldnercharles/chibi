# Public large-dependency importer proof

This test-only checkpoint demonstrates the importer fingerprint-bound propagation
on the production sources at `69cb3f268baaeecc1e34c3add22aa836f072f2b4`.
It changes no production code, limits, ABI, wire format, compiler seed or runtime
pin. It is a development checkpoint for review, not merge or release approval.

## Public input

The only large input is the existing public
[compiler bootstrap at 0721474e](https://github.com/waldnercharles/chibi/blob/0721474e386b0b068a2980f4a022c9563ea5049e/tools/compiler/bootstrap/compiler.chibip).

| Identity | Value |
|---|---|
| Commit | `0721474e386b0b068a2980f4a022c9563ea5049e` |
| Tree | `047b95101f10df55de3e014ee8c3488604507dd8` |
| Closure size | 15,499,688 bytes; 12 modules |
| Closure SHA256 | `3c20a8b4271f60b6b54f9f9dcad8d39ee39510e5f631d84f6daeb7469123b329` |
| Module | `project:package_compiler`, ID `0x8fb3be3da78425e0` |
| Raw module size | 13,683,482 bytes |
| Raw module SHA256 | `c1dd7ff228cc63904523962fefadce4e0deaab8d2e3801eb8231c0bca48723ce` |
| Raw module FNV-1a | `0x97aa285aa6528a26` |
| Target | prepared 21, 8-byte pointers, ABI `0x971f6da71ac0a8a0` |

Earlier controlled qualification loaded these same intact bytes at 4,000,000
records and rejected them at 1,000,000, changing no other bounds. All 20 public
manifest source size/hash entries matched that commit. This established a real
large-record closure without inferring record count from file size. No exact
per-module record counter was measured.

This proof applies only to the public input above. It does **not** verify the
separately reported 17,899,628-byte asset compiler artifact or its historical
controls.

## What the test proves

`model.inc` reuses the typed model/marshal and field helpers from
`test/language/prepared_writer_chibi.c`, plus the boxing helpers from
`compiler_fixture.c`. Only the authoring metadata lookup is adapted: DTO records
are resolved by checked stable ID/name from the already-decoded Writer module.
No source authoring corpus is compiled. `adapter.c` copies the existing
`sparse_definition(2, 1)` and uses the existing pico_unit dispatcher.

1. Production `Writer.Write` emits the tiny no-import module. Its 266 bytes must
   exactly match native `execution_prepared_write`, including the target ABI.
2. Add one supported typed `Import` DTO to a fresh `Module.Tables` value, using
   the raw dependency module's FNV-1a. No wire bytes are patched. Production
   `Writer.Write` emits a 286-byte importer, SHA256
   `a8806179d93a4089a871cf57514b14f4ef539fa9c54a1be4b4657137eaa003a3`.
3. Read the original public rows plus the tiny importer through production
   `package_read_budget`. Successful reading compares the DTO fingerprint with
   native `dependency_fingerprint` canonical re-encoding; a file-level SHA256
   is never substituted for that fingerprint.
4. Invoke only the importer-owned tiny entry. Require **7 → 14**, successful
   return, actual bytecode entries and no compiled entries. The observed count
   is exactly one; the tiny body has no calls or host calls.
5. Link a second host with exactly this line removed from a scratch copy of
   `src/execution/prepared.c`:

   ```c
   c.wire.record_limit = budget ? budget->record_limit : 0;
   ```

   The mutant must first load the original 12-module closure successfully.
   Only after adding the tiny importer may it reject, naming
   `project:capacity_importer` and the record bound, with the entire aggregate
   budget restored. This distinguishes the dependency fingerprint stage from
   an earlier failure loading the original closure.
6. Run the unchanged original host again and require restored success.

Emission invokes only Writer.Write. Runtime validation invokes only the tiny
importer entry; no compiler or self-compilation entry is invoked. The hosts share
the same adapter object and libraries; only the mutant translation unit differs.

## Recorded result

`receipt.json` contains selected observations and exact source/host hashes from
the owner-executed replay. It is not a complete raw log archive or an independent
reviewer rerun. The recorded resource evidence below applies to the single-process
adapter cases only. The longest final runtime was **1.707 seconds**; maximum `time -l` RSS was
**253,607,936 bytes**, and maximum sampled aggregate descendant RSS was
**248,848 KiB**, below the 768 MiB ceiling. Each case passed: emission **45 assertions**, restored-before **18**, expected
mutant rejection **9**, restored-after **18**. The mutant process exits zero
because its expected-rejection assertions pass; its combined loader returns
failure and leaves **268,435,456 bytes** remaining. Both restored runs load 13
modules and charge **111,625,778 bytes**, leaving **156,809,678**.

The execution owner ran the tests. Independent source/receipt review inspected the
adapter, typed DTO construction, fingerprint comparison, single-line mutation,
link commands, backend assertions and retained raw receipts. Reviewers did not
run builds or tests. Review identified resource-wrapper failure paths that were
corrected and tested before publication; the remaining scope limitation is
documented below. No verification of that separately reported
artifact is claimed by either review.

## Reproduce on macOS

Use Python 3, CMake, Ninja and a C23-capable Clang. The recorded configuration was
Debug, Clang 23.1.1, arm64, no sanitizers, SJLIT disabled and dead stripping enabled.
Reuse the repository's existing third-party archive cache and SDK objects.
The script requires an SDK build configured against this checkout and rejects
production source changes from the reviewed base.

From the repository root, choose a disposable work directory and obtain the
exact existing public bytes (this does not regenerate the bootstrap):

```sh
CHIBI_IMPORTER_WORK="$(mktemp -d)"
git fetch origin 0721474e386b0b068a2980f4a022c9563ea5049e
git show 0721474e386b0b068a2980f4a022c9563ea5049e:tools/compiler/bootstrap/compiler.chibip > "$CHIBI_IMPORTER_WORK/compiler.chibip"
```

If no suitable SDK build exists, set `CHIBI_TEST_CC` to your Clang executable and
`CHIBI_TEST_ARCHIVE_CACHE` to the existing third-party archive cache, then configure
under the same resource ceiling:

```sh
python3 test/language/public_importer/bounded.py "$CHIBI_IMPORTER_WORK/configure" 120 768 \
  cmake -S sdk -B "$CHIBI_IMPORTER_WORK/sdk" -G Ninja \
  -DCMAKE_C_COMPILER="$CHIBI_TEST_CC" -DCMAKE_BUILD_TYPE=Debug \
  -DCHIBI_ENABLE_SJLIT=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_EXE_LINKER_FLAGS=-Wl,-dead_strip \
  -DCHIBI_THIRD_PARTY_CACHE="$CHIBI_TEST_ARCHIVE_CACHE"
```

Inspect the minimal library target first. The reviewed graph contained no
embedded-asset generation or full compiler self-build. Stop if that changes.
Do not substitute the full language test build or raise the limits.

```sh
ninja -C "$CHIBI_IMPORTER_WORK/sdk" -t commands chibi_compiler_host
python3 test/language/public_importer/run.py \
  --build-dir "$CHIBI_IMPORTER_WORK/sdk" \
  --work-dir "$CHIBI_IMPORTER_WORK/proof" \
  --artifact "$CHIBI_IMPORTER_WORK/compiler.chibip" --plan
```

For retained SDK objects, replace only `--build-dir` with that configured build.
Remove `--plan` to execute. The script updates only the minimal runtime library
closure, compiles the test adapter and one mutant translation unit, then runs:

```text
restored --emit PUBLIC_CLOSURE IMPORTER_OUTPUT
restored --check PUBLIC_CLOSURE IMPORTER_OUTPUT
mutant --check-mutant PUBLIC_CLOSURE IMPORTER_OUTPUT
restored --check PUBLIC_CLOSURE IMPORTER_OUTPUT
```

The wrapper is configured for **768 MiB aggregate observed descendant RSS** and
**120 seconds** per sequential stage. Loading keeps **4,000,000 records**, **256 MiB** per-module and
aggregate storage, **256M layout-work units**, and the existing **256 MiB verifier
ceiling**. The wrapper tracks observed descendants across process groups and
treats any resource or monitoring stop as failure. It does not guarantee
accounting or cleanup of detached or reparented children after normal root exit;
it is not a general process supervisor. This recorded resource evidence is
limited to the single-process adapter cases. Runtime RSS sampling and `time -l`
peaks are reported separately; short processes can escape a polling sample.

Full commands, raw logs, identities and generated files stay in the chosen work
area; do not publish logs containing local paths. Executable hashes can change
with toolchain, build directory and debug metadata; input/importer hashes and
semantic assertions are stable. Remove generated executables after review;
retain only necessary receipts and reusable compilation intermediates. No large
binary, executable or private log is included in this checkpoint.
