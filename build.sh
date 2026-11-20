#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"
case "$(uname -s)" in
    Linux) set -- -shared -ldl ;;
    Darwin) set -- -bundle -undefined dynamic_lookup ;;
    *) printf '%s\n' 'This build script supports Linux and macOS.' >&2; exit 1 ;;
esac
# Replace the library atomically so open Bash sessions can keep their old mapping.
tmp_library=$(mktemp ./bash-mouse-position.XXXXXXXX)
trap 'rm -f "$tmp_library"' 0
"${CC:-cc}" -std=c99 -O2 -Wall -Wextra -fPIC -o "$tmp_library" bash-mouse-position.c "$@"
mv -f "$tmp_library" bash-mouse-position.so
