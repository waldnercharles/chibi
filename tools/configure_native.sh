#!/bin/sh
set -eu
exec sh "$(dirname "$0")/configure.sh" native "$@"
