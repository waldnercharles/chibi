#!/bin/bash
set -euo pipefail
# Install clang-format
python3 -m venv /tmp/clang-format
/tmp/clang-format/bin/pip install --only-binary=:all: clang-format==22.1.0
export PATH=/tmp/clang-format/bin:$PATH


# Check format
sh tools/clang-format.sh --check

# Check Chibi sources for empty operands
if git grep -nE '(==|!=|\|\||&&) *(\|\||&&|\))|\( *(\|\||&&)' -- '*.chibi' ':!archive'; then
  echo 'An operator above has no operand; delete the whole condition with the path.'
  exit 1
fi


# Check test selectors name existing driver flags
status=0
while IFS="$(printf '\t')" read -r kind name driver flag rest; do
  [ "$kind" = tool ] || continue
  case "$driver" in
    */test_tools) source=test/tools/main.c ;;
    */test_language) source=test/language/main.c ;;
    *) continue ;;
  esac
  case "$flag" in --*) ;; *) continue ;; esac
  grep -qF -- "\"$flag\"" "$source" || { echo "tools/tests.tsv: $name runs $flag, which $source does not accept"; status=1; }
done < tools/tests.tsv
test "$status" = 0


# Small CI infrastructure regressions; no compiler or language fixtures.
node --test test/ci/*.test.js
