#!/bin/bash

cd "$(dirname "$0")" || exit 1

DIRS=("src" "src/std" "ssl")
EXTS=("*.c" "*.h" "*.cpp" "*.hpp")

pids=()

for dir in "${DIRS[@]}"; do
    if [ -d "$dir" ]; then
        echo "Formatting: $dir"
        for ext in "${EXTS[@]}"; do
            find "$dir" -type f -name "$ext" -exec clang-format -i {} + &
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