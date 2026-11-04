#!/bin/sh
# Focused audit-oracle regression; never invokes the managed/task suite.
set -eu
compiler=${1:?native compiler}
llvm=${2:?LLVM directory}
work=${3:?owned workspace}/source-audit
root=$(CDPATH='' cd -- "$(dirname "$0")/../../.." && pwd -P)
fixtures=$root/test/native_aot/managed_pointers
# An alternate audit permits the same test to prove the pre-fix failure.
audit=${4:-$fixtures/SourceAudit.ll}
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'source audit regression: unsupported test host' >&2; exit 2 ;;
esac
mkdir -p "$work"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$fixtures/SourceProbe.chibi" --entry Main --output "$work/SourceProbe" \
    --llvm "$llvm" --target "$triple" --jobs 1
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$fixtures/NativeProbe.chibi" --entry AuditTrapOracle --output "$work/AuditTrapOracle" \
    --llvm "$llvm" --target "$triple" --jobs 1
"$work/AuditTrapOracle"
for name in Cells AssertionCleanup; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$fixtures/source/$name.chibi" --entry Main --emit-llvm \
        --target "$triple" --output "$work/$name.ll"
done
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$fixtures/source/NestedPointers.chibi" --entry Failure --emit-llvm \
    --target "$triple" --output "$work/NestedPointerFailure.ll"
python3 -I -S -B - "$work" "$llvm" "$audit" <<'PY'
from pathlib import Path
import re
import subprocess
import sys

work, llvm, audit = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
text = audit.read_text()
bound = "%bounded = icmp ule i64 %peak, 2048"
assert text.count(bound) == 1, "missing unique peak bound"
tight = work / "zero-peak-audit.ll"
tight.write_text(text.replace(bound, "%bounded = icmp ule i64 %peak, 0"))
for callbacks in (False, True):
    # AssertionCleanup has no collector; callback hooks need the managed
    # collector emitted by the existing nested-pointer failure fixture.
    failure = "NestedPointerFailure" if callbacks else "AssertionCleanup"
    for name, assertion, invalid in (("Cells", False, False),
                                     (failure, True, False),
                                     (failure, True, True)):
        directory = work / f"{name}-callbacks-{callbacks}-invalid-{invalid}"
        directory.mkdir(exist_ok=True)
        command = [str(work / "SourceProbe")]
        if callbacks:
            command.append("--callbacks")
        if assertion:
            command.append("--assertion")
        command += [str(directory), llvm, str(tight if invalid else audit),
                    str(work / f"{name}.ll")]
        result = subprocess.run(command, capture_output=True, text=True)
        (directory / "stdout.log").write_text(result.stdout)
        (directory / "stderr.log").write_text(result.stderr)
        log = result.stdout + result.stderr
        reports = re.findall(r"managed source live (\d+) peak (\d+) total (\d+)", log)
        passes = re.findall(r"^PASS managed source .* force=(?:false|true) O[02] poisoned balance$", log, re.M)
        assert all(int(live) == 0 and 0 < int(peak) <= 2048 for live, peak, _ in reports), log
        if invalid:
            # A balanced real assertion exceeds only the artificial zero bound.
            # The old audit is accepted four times; the fixed one stops at O0.
            assert result.returncode == 1 and len(reports) == 1 and not passes, log
            assert re.search(r"force=false O0 status=(-4|-5|132|133) ", log), log
            assert "native failure: AssertionFailed" not in log, log
        else:
            assert result.returncode == 0 and len(reports) == len(passes) == 4, log
            assert log.count("native failure: AssertionFailed") == (4 if assertion else 0), log
        print(f"PASS source audit {name} callbacks={callbacks} rejects-invalid={invalid}")
PY
