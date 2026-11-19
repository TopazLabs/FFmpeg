#!/bin/bash

rm -rf ./conan

TENSORRT_RTX=${1:-False}

if [ "$TENSORRT_RTX" = "True" ]; then
    echo "Building with TensorRT RTX"
else
    echo "Building with TensorRT Enterprise"
fi

BUILD_LINUX_GPL=${2:-False}

if [ "$BUILD_LINUX_GPL" = "True" ]; then
    echo "Building with GPL software encoders (libx264, libx265)"
fi

# profile_ubuntu24.04 (gcc-14, LinuxManaged agents) or profile_ubuntu22.04 (gcc-11, UbuntuVMSS agents)
PROFILE=${3:-profile_ubuntu24.04}
echo "Conan profile: $PROFILE"

conan install ./build-scripts/conanfile.py -u -pr:b "./build-scripts/linux/$PROFILE" -pr:h "./build-scripts/linux/$PROFILE" -of ./conan -o "&:tensorrt_rtx=$TENSORRT_RTX" -o "&:build_linux_GPL=$BUILD_LINUX_GPL"
