#!/usr/bin/env bash
# Builds LayerTack (Release) into build/. Linux: needs cmake, a C++17 compiler and libsdl2-dev
# (sudo apt install cmake g++ libsdl2-dev). Then ./build/LayerTack
set -e
cd "$(dirname "$0")"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc 2>/dev/null || echo 4)"
echo "Built: build/LayerTack"
