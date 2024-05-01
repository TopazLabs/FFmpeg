#!/bin/bash

rm -rf ./conan_x64
rm -rf ./conan_arm

conan install ./build-scripts/conanfile.py -u -pr:h ./build-scripts/mac/profile_mac14.0 -pr:b ./build-scripts/mac/profile_mac_armv8 -of ./conan_x64/
conan install ./build-scripts/conanfile.py -u -pr:h ./build-scripts/mac/profile_mac_armv8 -pr:b ./build-scripts/mac/profile_mac_armv8 -of ./conan_arm/
