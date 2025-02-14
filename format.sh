#!/bin/bash

cd "$(dirname "$0")" || exit 1

DIRS=("src" "include" "std" "common")
EXTS=("*.c" "*.h" "*.cpp" "*.hpp")

for dir in "${DIRS[@]}"; do
    if [ -d "$dir" ]; then
        echo "Formatting: $dir"
        for ext in "${EXTS[@]}"; do
            find "$dir" -type f -name "$ext" -exec clang-format -i {} + &
        done
    else
        echo "$dir: File or directory inexistent"
    fi
done

# All the clang-format commands were executed in parallel (The & after the command execs it in parallel)
# Wait for all of them to succeed
wait