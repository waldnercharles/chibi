#!/bin/sh
# Explicit repository-local opt-in. Never replace a user's custom hooks.
set -eu
root=$(git rev-parse --show-toplevel)
cd "$root"
existing=$(git config --get core.hooksPath || :)
if [ "$existing" = .githooks ]; then
    echo 'Repository hooks are already selected.'
    exit 0
fi
if [ -n "$existing" ]; then
    echo "Refusing to replace existing core.hooksPath=$existing. Integrate .githooks/pre-commit into your hook manually." >&2
    exit 1
fi
hooks=$(git rev-parse --git-path hooks)
if [ -e "$hooks/pre-commit" ]; then
    echo 'Refusing to bypass an existing pre-commit hook. Integrate .githooks/pre-commit manually.' >&2
    exit 1
fi
git config --local core.hooksPath .githooks
echo 'Enabled repository hooks locally. Chibi source formatting now runs before normal Git commits.'
