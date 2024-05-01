#!/bin/bash

rm -rf ./conan

conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/win/profile_win2019 -pr:h ./build-scripts/win/profile_win2019 -of ./conan
