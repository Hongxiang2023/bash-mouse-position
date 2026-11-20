#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"
case "$(uname -s)" in
    Linux) set -- -shared ;;
    Darwin) set -- -bundle -undefined dynamic_lookup ;;
    *) printf '%s\n' 'This build script supports Linux and macOS.' >&2; exit 1 ;;
esac
"${CC:-cc}" -std=c99 -O2 -Wall -Wextra -fPIC "$@" -o bash-mouse-position.so bash-mouse-position.c
