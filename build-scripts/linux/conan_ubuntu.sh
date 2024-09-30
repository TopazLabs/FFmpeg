#!/bin/bash

rm -rf ./conan

conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/linux/profile_ubuntu22.04 -pr:h ./build-scripts/linux/profile_ubuntu22.04 -of ./conan
