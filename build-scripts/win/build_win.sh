#!/bin/bash

set -e

# Navigate to sources directory
cd "$(dirname "$0")/../../"
source ./build-scripts/win/setup-msvc-toolchain.sh
echo "$PATH"
echo "$PKG_CONFIG_PATH"
./configure --toolchain=msvc --prefix=output-conan --enable-libvpx --enable-libaom --enable-shared --enable-x86asm --x86asmexe=yasm --enable-nvenc --enable-nvdec --disable-vulkan --enable-amf --enable-libvpl --enable-zlib --enable-tvai --extra-cflags="-I./conan/lib3rdparty/videoai/include -I./conan/lib3rdparty/amf/include -I./conan/lib3rdparty/nv-codec-headers/include -I./conan/lib3rdparty/libvpx/include -I./conan/lib3rdparty/aom/include -I./conan/lib3rdparty/libvpl/include/vpl" --extra-ldflags="-libpath:./conan/lib3rdparty/videoai/lib -libpath:./conan/lib3rdparty/zlib/lib -libpath:./conan/lib3rdparty/libvpx/lib -libpath:./conan/lib3rdparty/aom/lib -libpath:./conan/lib3rdparty/libvpl/lib -incremental:no"
make clean
make -r -j$(nproc) install