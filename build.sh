#!/bin/bash

cd "$(dirname "$0")" || exit 1

mkdir -p build
make -j # or CMake depending on your preferences
        # I find make to be much more manageable.