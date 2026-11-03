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


# Small CI infrastructure regressions; no compiler or language fixtures.
node --test test/ci/*.test.js
