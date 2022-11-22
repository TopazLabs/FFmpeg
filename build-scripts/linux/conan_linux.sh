#!/bin/bash

rm -rf ./conan

conan install ./build-scripts/conanfile.py --update -pr ./build-scripts/linux/profile_ubuntu22.04 -if ./conan
