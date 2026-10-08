#!/bin/bash

set -e

ARCH=${1:?"Missing ARCH argument. ARCH=ARM64 or ARCH=AMD64"}

# Ensure all expected system dependencies
pacman -S --noconfirm base-devel

# Navigate to sources directory
cd "$(dirname "$0")/../../../"
# Navigate to nv-codec-headers directory
cd "./nv-codec-headers"
make install
# Navigate to FFmpeg directory (from nv-codec-headers directory)
cd "../FFmpeg"
source ./build-scripts/win/setup-msvc-toolchain.sh ${ARCH}

if [[ "${ARCH}" == "AMD64" ]]; then
    ./configure --toolchain=msvc --prefix=output-conan --enable-libvpx --enable-libdav1d --enable-libaom --enable-shared --enable-x86asm --x86asmexe=nasm --enable-nvenc --enable-nvdec --disable-vulkan --enable-amf --disable-filter=amf_capture --enable-libvpl --enable-zlib --enable-libzimg --enable-tvai --extra-cflags="-I./conan/lib3rdparty/videoai/include/videoai -I./conan/lib3rdparty/amf/include -I./conan/lib3rdparty/libvpx/include -I./conan/lib3rdparty/dav1d/include -I./conan/lib3rdparty/libaom-av1/include -I./conan/lib3rdparty/libvpl/include/vpl -I./conan/lib3rdparty/zlib-mt/include/ -I./conan/lib3rdparty/zimg/include/ -MD" --extra-ldflags="-libpath:./conan/lib3rdparty/videoai/lib -libpath:./conan/lib3rdparty/zlib-mt/lib -libpath:./conan/lib3rdparty/libvpx/lib -libpath:./conan/lib3rdparty/dav1d/lib -libpath:./conan/lib3rdparty/libaom-av1/lib -libpath:./conan/lib3rdparty/libvpl/lib -libpath:./conan/lib3rdparty/zimg/lib -incremental:no"
elif [[ "${ARCH}" == "ARM64" ]]; then # works for native arm64 builds
    ./configure --toolchain=msvc --prefix=output-conan --enable-mediafoundation --enable-d3d11va --enable-dxva2 --enable-libvpx --enable-libaom --enable-shared --disable-x86asm --enable-nvenc --enable-nvdec --disable-vulkan --disable-amf --disable-libvpl --enable-zlib --enable-libzimg --enable-tvai --extra-cflags="-I./conan/lib3rdparty/videoai/include/videoai -I./conan/lib3rdparty/libvpx/include -I./conan/lib3rdparty/libaom-av1/include -I./conan/lib3rdparty/zlib-mt/include/ -I./conan/lib3rdparty/zimg/include/" --extra-ldflags="-libpath:./conan/lib3rdparty/videoai/lib -libpath:./conan/lib3rdparty/zlib-mt/lib -libpath:./conan/lib3rdparty/libvpx/lib -libpath:./conan/lib3rdparty/libaom-av1/lib -libpath:./conan/lib3rdparty/zimg/lib -incremental:no"
fi

make clean
make -r -j$(nproc) install
