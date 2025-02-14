#!/bin/bash

cd "$(dirname "$0")" || exit 1

make --always-make --dry-run | \
grep -wE 'gcc|g\+\+|clang' | \
grep -w '\-o' | \
grep -v '\-l' | \
jq -nR '[inputs | {directory: "'"$(pwd)"'", command: ., file: (match(" [^ ]+\\.(c|cpp|h|hpp)").string[1:] // null) | sub("^"; "'"$(pwd)/"'")} ]'\
> build/compile_commands.json

if [ ! -f build/compile_commands.json ]; then
    echo "compile_commands.json was not found. exiting."
    exit 1
fi

find src -type f -name '*.c' -exec clang-tidy -p build/compile_commands.json -fix {} +
