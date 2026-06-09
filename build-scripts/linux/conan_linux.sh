#!/bin/bash

rm -rf ./conan

host_arch=$(uname -m)
arch=${ARCH:-$host_arch}

case "$arch" in
  ARM64|arm64|aarch64)
    arch=armv8
    ;;
  AMD64|amd64)
    arch=x86_64
    ;;
esac

if [ "$arch" = "armv8" ]; then
  echo "Installing conan packages for armv8"
  if [ "$host_arch" = "aarch64" ]; then
    conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/linux/profile_ubuntu22.04_armv8 -pr:h ./build-scripts/linux/profile_ubuntu22.04_armv8 -o "videoai/*:with_rlm=False" -o "videoai/*:aiengine_version=3.8.26-dgx" -of ./conan
  else
    conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/linux/profile_ubuntu22.04 -pr:h ./build-scripts/linux/profile_ubuntu22.04_armv8_cross -o "videoai/*:with_rlm=False" -o "videoai/*:aiengine_version=3.8.26-dgx" -of ./conan
  fi
elif [ "$arch" = "x86_64" ]; then
  echo "Installing conan packages for x86_64"
  conan install ./build-scripts/conanfile.py -u -pr:b ./build-scripts/linux/profile_ubuntu22.04 -pr:h ./build-scripts/linux/profile_ubuntu22.04 -of ./conan
else
  echo "Unsupported architecture: $arch"
  exit 1
fi

