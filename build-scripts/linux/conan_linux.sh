#!/bin/bash

rm -rf ./conan

if lscpu | grep -Eq '^Architecture:[[:space:]]*aarch64$'; then
  conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/linux/profile_ubuntu22.04_armv8 -pr:h ./build-scripts/linux/profile_ubuntu22.04_armv8 -of ./conan
else
  conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/linux/profile_ubuntu22.04 -pr:h ./build-scripts/linux/profile_ubuntu22.04 -of ./conan
fi

