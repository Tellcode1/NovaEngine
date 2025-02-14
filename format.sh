#!/bin/bash

DIRS=("src" "include" "std" "common")
EXTS=("*.c" "*.h" "*.cpp" "*.hpp")

for dir in "${DIRS[@]}"; do
    if [ -d "$dir" ]; then
        echo "Formatting: $dir"
        for ext in "${EXTS[@]}"; do
            find "$dir" -type f -name "$ext" -exec clang-format -i {} +
        done
    else
        echo "$dir: File or directory inexistent"
    fi
done