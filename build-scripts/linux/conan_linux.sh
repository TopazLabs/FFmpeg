#!/bin/bash

rm -rf ./conan

build_linux_gpl="${BUILD_LINUX_GPL:-false}"
build_linux_gpl="${build_linux_gpl,,}"

conan_option="build_linux_GPL=False"
if [[ "${build_linux_gpl}" == "true" ]]; then
  conan_option="build_linux_GPL=True"
fi

conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/linux/profile_ubuntu22.04 -pr:h ./build-scripts/linux/profile_ubuntu22.04 -of ./conan -o "${conan_option}"
