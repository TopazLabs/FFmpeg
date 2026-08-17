#!/bin/bash

rm -rf ./conan

TENSORRT_RTX=${1:-True}

if [ "$TENSORRT_RTX" = "True" ]; then
    echo "Building with TensorRT RTX"
else
    echo "Building with TensorRT Enterprise"
fi

conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/win/profile_win2022 -pr:h ./build-scripts/win/profile_win2022 -of ./conan -o videoai/*:tensorrt_rtx=$TENSORRT_RTX
