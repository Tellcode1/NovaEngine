#!/bin/bash

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
ROOT_DIR="$SCRIPT_DIR/.."

cd "$ROOT_DIR"

DIRS=("src")
EXTS=("*.c" "*.h" "*.cpp" "*.h")

pids=()

for dir in "${DIRS[@]}"; do
    if [ -d "$dir" ]; then
        echo "Formatting: $dir"
        for ext in "${EXTS[@]}"; do
            find "$dir" \( -type d -name "external" -o -type d -name "build" \) -prune -o -type f -name "$ext" -print -exec clang-format -i {} + &
            pids+=($!)
        done
    else
        echo "$dir: File or directory inexistent"
    fi
done

# All the clang-format commands were executed in parallel (The & after the command execs it in parallel)
# Wait for all of them to succeed
for pid in "${pids[@]}"; do
    wait "$pid"
done