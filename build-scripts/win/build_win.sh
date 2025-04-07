#!/bin/bash

set -e

# Ensure all expected system dependencies
pacman -S --noconfirm base-devel

# Navigate to sources directory
cd "$(dirname "$0")/../../../"
# Navigate to nv-codec-headers directory
cd "./nv-codec-headers"
make install
# Navigate to FFmpeg directory (from nv-codec-headers directory)
cd "../FFmpeg"
source ./build-scripts/win/setup-msvc-toolchain.sh
./configure --toolchain=msvc --prefix=output-conan --enable-libvpx --enable-libaom --enable-shared --enable-x86asm --x86asmexe=yasm --enable-nvenc --enable-nvdec --disable-vulkan --enable-amf --enable-libvpl --enable-zlib --enable-libzimg --enable-tvai --extra-cflags="-I./conan/lib3rdparty/videoai/include/videoai -I./conan/lib3rdparty/amf/include -I./conan/lib3rdparty/libvpx/include -I./conan/lib3rdparty/aom/include -I./conan/lib3rdparty/libvpl/include/vpl -I./conan/lib3rdparty/zlib-mt/include/ -I./conan/lib3rdparty/zimg/include/" --extra-ldflags="-libpath:./conan/lib3rdparty/videoai/lib -libpath:./conan/lib3rdparty/zlib-mt/lib -libpath:./conan/lib3rdparty/libvpx/lib -libpath:./conan/lib3rdparty/aom/lib -libpath:./conan/lib3rdparty/libvpl/lib -libpath:./conan/lib3rdparty/zimg/lib -incremental:no"
make clean
make -r -j$(nproc) install
