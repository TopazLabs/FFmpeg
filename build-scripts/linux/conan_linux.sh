#!/bin/bash

rm -rf ./conan

TENSORRT_RTX=${1:-True}

if [ "$TENSORRT_RTX" = "True" ]; then
    echo "Building with TensorRT RTX"
else
    echo "Building with TensorRT Enterprise"
fi

conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/linux/profile_ubuntu22.04 -pr:h ./build-scripts/linux/profile_ubuntu22.04 -of ./conan -o videoai/*:tensorrt_rtx=$TENSORRT_RTX
