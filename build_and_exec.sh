#!/bin/bash
mkdir -p build && cd build && cmake .. -DNOVA_BUILD_EXAMPLE=1 && make -j
./fontc -i ../Assets/roboto.ttf -o bakedfont -p 64; ./fontc -i ../Assets/OpenSans.ttf -o ./OpenSans.ff -p 64;
./nova_example