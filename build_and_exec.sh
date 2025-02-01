#!/bin/bash
mkdir -p build && cd build && cmake .. -DNOVA_BUILD_EXAMPLE=1 && make -j && ./fontc ../Assets/roboto.ttf && ./nova_example