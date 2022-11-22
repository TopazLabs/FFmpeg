#!/bin/bash

rm -rf ./conan

conan install ./build-scripts/conanfile.py --update -pr ./build-scripts/mac/profile_ubuntu22.04 -if ./conan
