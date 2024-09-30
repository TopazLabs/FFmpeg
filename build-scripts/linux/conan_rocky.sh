#!/bin/bash

rm -rf ./conan

conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/linux/profile_rocky8.10 -pr:h ./build-scripts/linux/profile_rocky8.10 -of ./conan
