#!/bin/sh
# Syntax-check every decompiled source against the inferred headers.
#
# This proves the sources parse and that the types they name exist and line up.
# It does NOT prove behaviour: there is no verification harness, and the layouts
# in include/gh2 are documentation, not the PS2 ABI.
set -e
cd "$(dirname "$0")/.."
fail=0
for f in $(find src -name '*.cpp' | sort); do
    if clang++ -std=c++11 -fsyntax-only -Wall -Iinclude "$f"; then
        echo "ok   $f"
    else
        echo "FAIL $f"
        fail=1
    fi
done
exit $fail
