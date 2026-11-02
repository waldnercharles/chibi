#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
    echo 'usage: run.sh NATIVE_DRIVER LLVM_BIN' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
driver=$1
llvm=$2
scratch=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-test-attributes.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
compile() {
    "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$scratch/$1.chibi" --output "$scratch/program" --llvm "$llvm" --jobs 2
}
cat > "$scratch/Tests.chibi" <<'CHIBI'
package tests
import os "core:os"

// Test functions remain void; the caller supplies the ordinary native entry.
Main :: () -> int {
    arguments := os.Arguments();
    if (arguments[0] == "fail") {
        Failing();
    } else if (arguments[0] == "another") {
        Another();
    } else {
        Passing();
    }
    return 0;
}

Helper :: (value: int) -> int { return value + 1; }

@test
Passing :: () { assert(Helper(41) == 42); }

@test
Failing :: () { assert(false); }

@test
Another :: () { assert(Helper(2) == 3); }
CHIBI
compile Tests
"$scratch/program" pass
set +e
"$scratch/program" fail > "$scratch/failure.log" 2>&1
status=$?
set -e
test "$status" -eq 134
grep -q 'assertion failed' "$scratch/failure.log"
"$scratch/program" another

# Match chibi test's package shape: a legal entry calls an injected public
# dispatcher, which can see a private @test and helper in its own package.
mkdir "$scratch/cases"
cat > "$scratch/PackageMain.chibi" <<'CHIBI'
package runner
import tested "project:cases"
Main :: () -> int { tested.ChibiRunTest(0); return 0; }
CHIBI
cat > "$scratch/cases/Cases.chibi" <<'CHIBI'
package cases
Helper :: (value: int) -> int { return value + 1; }
@test
Example :: () { assert(Helper(41) == 42); }
pub ChibiRunTest :: (index: int) { if (index == 0) { Example(); } }
CHIBI
"$driver" --compiler "$scratch" --core "$root/packages/core" \
    --source "$scratch/PackageMain.chibi" --output "$scratch/program" --llvm "$llvm" --jobs 2
"$scratch/program"
sed 's/@test/@unknown/' "$scratch/cases/Cases.chibi" > "$scratch/cases/rejected"
mv "$scratch/cases/rejected" "$scratch/cases/Cases.chibi"
printf 'preserve existing output\n' > "$scratch/program"
cp "$scratch/program" "$scratch/expected"
set +e
"$driver" --compiler "$scratch" --core "$root/packages/core" \
    --source "$scratch/PackageMain.chibi" --output "$scratch/program" --llvm "$llvm" --jobs 2 \
    > "$scratch/imported.log" 2>&1
status=$?
set -e
test "$status" -eq 1
grep -F "Unknown attribute '@unknown' (cases/Cases.chibi:3)" "$scratch/imported.log"
cmp "$scratch/program" "$scratch/expected"
test ! -e "$scratch/program.0.ll"

cat > "$scratch/Unknown.chibi" <<'CHIBI'
package tests
Main :: () -> int { return 0; }
@unknown
Case :: () {}
CHIBI
cat > "$scratch/Duplicate.chibi" <<'CHIBI'
package tests
Main :: () -> int { return 0; }
@test
@test
Case :: () {}
CHIBI
cat > "$scratch/Parameters.chibi" <<'CHIBI'
package tests
Main :: () -> int { return 0; }
@test
Case :: (value: int) {}
CHIBI
cat > "$scratch/Results.chibi" <<'CHIBI'
package tests
Main :: () -> int { return 0; }
@test
Case :: () -> int { return 1; }
CHIBI
cat > "$scratch/NamedResults.chibi" <<'CHIBI'
package tests
Main :: () -> int { return 0; }
@test
Case :: () -> (result: int) { return 1; }
CHIBI
cat > "$scratch/Malformed.chibi" <<'CHIBI'
package tests
Main :: () -> int { return 0; }
@test(1)
Case :: () {}
CHIBI
reject() {
    name=$1
    expected=$2
    printf 'preserve existing output\n' > "$scratch/program"
    cp "$scratch/program" "$scratch/expected"
    set +e
    compile "$name" > "$scratch/$name.log" 2>&1
    status=$?
    set -e
    test "$status" -eq 1
    grep -F "$expected" "$scratch/$name.log"
    cmp "$scratch/program" "$scratch/expected"
    test ! -e "$scratch/program.0.ll"
}
reject Unknown "Unknown attribute '@unknown'"
reject Duplicate "Duplicate attribute '@test'"
reject Parameters 'A @test function takes no parameters and returns nothing'
reject Results 'A @test function takes no parameters and returns nothing'
reject NamedResults 'A @test function takes no parameters and returns nothing'
reject Malformed 'Expected a name, found `(`'

# Marking a void test does not silently relax the native () -> int entry ABI.
printf 'preserve existing output\n' > "$scratch/program"
cp "$scratch/program" "$scratch/expected"
set +e
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$scratch/Tests.chibi" --entry Passing --output "$scratch/program" \
    --llvm "$llvm" --jobs 2 > "$scratch/entry.log" 2>&1
status=$?
set -e
test "$status" -eq 1
grep -F 'scalar proof requires its first function to return int' "$scratch/entry.log"
cmp "$scratch/program" "$scratch/expected"
test ! -e "$scratch/program.0.ll"
echo 'native test attributes: passed'
