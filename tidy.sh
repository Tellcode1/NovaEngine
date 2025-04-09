#!/bin/bash

cd "$(dirname "$0")" || exit 1

# https://stackoverflow.com/a/72234354
# make all --always-make --dry-run CC=clang CXX=clang++ \
#  | grep -wE 'gcc|g\+\+|c\+\+|clang|clang\+\+' \
#  | grep -w '\-c' \
#  | jq -nR '[inputs|{directory:".", command:., file: match(" [^ ]+$").string[1:]}]' \
#  > build/compile_commands.json

echo $(dirname $0)

DIRECTORIES=(
    "src"
    "ssl"
)

if ! command -v clang-tidy &> /dev/null; then
    echo "clang-tidy not installed. install it you dumbass"
    exit 1
fi

if ! command -v clang &> /dev/null; then
    echo "clang is not installed or not directly executable. just mod this script to replace clang with wherever you put it"
    exit 1
fi

cmake -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ; make -j -C build/

for TARGET_DIR in "${DIRECTORIES[@]}"; do
    if [ ! -d "$TARGET_DIR" ]; then
        echo "Directory '$TARGET_DIR' does not exist. Skipping..."
        continue
    fi

    FILES=$(find "$TARGET_DIR" -type f \( -name "*.h" -o -name "*.c" \) ! -path "*/external/*")

    if [ -z "$FILES" ]; then
        continue
    fi

    for FILE in $FILES; do
        echo "Tidying $FILE"
        clang-tidy -header-filter='^(?!.*[/\\]external[/\\]).*$' -p build/ --config-file=.clang-tidy --fix-errors "$FILE" &

        # max number of parallel commands is set to 4
        if (( $(jobs -r | wc -l) >= 4 )); then
            wait -n
        fi
    done
done

wait