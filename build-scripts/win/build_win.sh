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

# CUDA filter kernels (rgb48_cuda, scale_cuda, ...) need clang with the NVPTX
# backend to compile .cu -> PTX (ffmpeg's cuda_llvm path; no CUDA SDK needed).
# Enable it explicitly when clang is available so configure fails loudly on a
# broken toolchain instead of silently dropping the filters.
CUDA_LLVM_FLAG=""
if command -v clang >/dev/null 2>&1; then
    CUDA_LLVM_FLAG="--enable-cuda-llvm"
    echo "clang found ($(command -v clang)): enabling cuda_llvm"
else
    echo "##vso[task.logissue type=error]clang not found on agent: CUDA filters (rgb48_cuda, scale_cuda, ...) will NOT be built. Install LLVM or the VS 'C++ Clang tools' component."
    exit 1
fi

./configure --toolchain=msvc --prefix=output-conan --disable-decoder=h264 --disable-decoder=hevc --enable-libvpx --enable-libdav1d --enable-libaom --enable-shared --enable-x86asm --x86asmexe=nasm --enable-nvenc --enable-nvdec --disable-vulkan --enable-amf --disable-filter=amf_capture --enable-libvpl --enable-zlib --enable-libzimg --enable-tvai $CUDA_LLVM_FLAG --extra-cflags="-I./conan/lib3rdparty/videoai/include/videoai -I./conan/lib3rdparty/amf/include -I./conan/lib3rdparty/libvpx/include -I./conan/lib3rdparty/dav1d/include -I./conan/lib3rdparty/libaom-av1/include -I./conan/lib3rdparty/libvpl/include/vpl -I./conan/lib3rdparty/zlib-mt/include/ -I./conan/lib3rdparty/zimg/include/ -MD" --extra-ldflags="-libpath:./conan/lib3rdparty/videoai/lib -libpath:./conan/lib3rdparty/zlib-mt/lib -libpath:./conan/lib3rdparty/libvpx/lib -libpath:./conan/lib3rdparty/dav1d/lib -libpath:./conan/lib3rdparty/libaom-av1/lib -libpath:./conan/lib3rdparty/libvpl/lib -libpath:./conan/lib3rdparty/zimg/lib -incremental:no"

# When cuda_llvm was requested, verify the CUDA filters actually made it in.
if [[ -n "$CUDA_LLVM_FLAG" ]]; then
    grep -q "#define CONFIG_RGB48_CUDA_FILTER 1" config_components.h || { echo "##vso[task.logissue type=error]rgb48_cuda filter not enabled by configure"; exit 1; }
fi

make clean
make -r -j$(nproc) install
