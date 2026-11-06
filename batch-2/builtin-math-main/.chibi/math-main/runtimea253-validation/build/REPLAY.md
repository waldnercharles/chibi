# Reproduce the audited lightweight validation

The original `validation-receipt.json` and `final-source-admission.json` remain historical, byte-for-byte evidence. Cleanup removes the frozen project clone and native outputs only. It retains generated header bytes and CMake metadata alongside source, dependency, object, archive and command hashes.

`source-recovery.json` records the repository, exact accepted base, source tree refs and exported patches. The source preparation ref points directly to tree `b4dbc77d99590a823361cddce8f81f08fddadb9e`. The final documented ref points to tree `0abd96c6b800a596c24d23a6e6719b15fe5e75d6` and changes only the results paragraph.

For a replay, create a new empty ignored directory in this repository, export the validated source ref there with `git archive`, and extract it with `tar -x -C`. Keep this evidence directory intact. Check every exported tracked file against `source-identity.json`; Git symlink hashes cover link text.

Use the exact argument arrays in `configure-result.json`, `build-result.json` and `selectors-result.json`, replacing only the old snapshot/build prefix with the new replay prefix. Set `CCACHE_DISABLE=1`. Keep Clang 22.1.8, Release, native/desktop OFF, asset-host guard OFF, sanitizers OFF, no compiler launcher or extra C flags. The accepted third-party archive remains external to this scratch and is not deleted.

Before building, run `ninja -n test_language`, `ninja -t commands test_language` and `ninja -t graph test_language` in the new build directory. Compare the planned sources and flags with `dependency-audit.json` and `dependency-commands.log`. Require 487 C compiles, 55 archives and the sole `test_language` link, with no producer commands. Only the nine canonical vendor C inputs have the repository's `-w`; project inputs require `-Wall -Wextra -Werror` and no added warning exceptions.

Build only `test_language` with four jobs. Run only the three recorded selectors serially through `tools/test_policy.py --jobs 1 native`: `--compiler-fixture`, `--package-imports`, `--package-tests`. The expected native result is 33 cases and 2,282 assertions. The small module adapter additionally reports 44 authored math tests on each VM. This does not run the full source compiler fixtures or any producer. Record new hashes and times rather than overwriting these historical receipts.

Use `ninja -t deps` after building to reproduce actual compiled header provenance. `compiled-input-identity.json` lists all original generated headers and tracked/toolchain inputs. `object-identity.json`, `archive-identity.json` and `validation-receipt.json` preserve the deleted outputs' SHA256 identities. Cleanup does not claim a rebuilt binary has the same path-sensitive hash.
