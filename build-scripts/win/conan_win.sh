#!/bin/bash

rm -rf ./conan

ARCH=${1:?"Missing ARCH argument. ARCH=ARM64 or ARCH=AMD64"}
TENSORRT_RTX=${2:-True}

if [ "$TENSORRT_RTX" = "True" ]; then
    echo "Building with TensorRT RTX"
else
    echo "Building with TensorRT Enterprise"
fi

if [[ "${ARCH}" == "ARM64" ]]; then
    HOST_PROFILE=./build-scripts/win/profile_win2022_armv8
elif [[ "${ARCH}" == "AMD64" ]]; then
    HOST_PROFILE=./build-scripts/win/profile_win2022
fi

conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/win/profile_win2022 -pr:h ${HOST_PROFILE} -of ./conan -o videoai/*:tensorrt_rtx=$TENSORRT_RTX
