#!/bin/bash

set -e

ARCH=${1:?"Missing ARCH argument. ARCH=ARM64 or ARCH=AMD64"}

# Ensure all expected system dependencies
pacman -S --noconfirm base-devel pkg-config

# Navigate to sources directory
cd "$(dirname "$0")/../../../"
# Navigate to nv-codec-headers directory
cd "./nv-codec-headers"
make install
# Navigate to FFmpeg directory (from nv-codec-headers directory)
cd "../FFmpeg"
source ./build-scripts/win/setup-msvc-toolchain.sh ${ARCH}

echo "build_win_local.sh: After sourcing setup script, PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
echo "build_win_local.sh: Testing pkg-config ffnvcodec:"
pkg-config --exists ffnvcodec && echo "ffnvcodec found by pkg-config" || echo "ffnvcodec NOT found by pkg-config"


make clean

if [[ "${ARCH}" == "AMD64" ]]; then
    ./configure --toolchain=msvc --prefix=output-conan --enable-libvpx --enable-libaom --enable-shared --enable-x86asm --x86asmexe=nasm --enable-nvenc --enable-nvdec --disable-vulkan --enable-amf --enable-libvpl --enable-zlib --enable-libzimg --enable-tvai --extra-cflags="-I./conan/lib3rdparty/videoai/include/videoai -I./conan/lib3rdparty/amf/include -I./conan/lib3rdparty/libvpx/include -I./conan/lib3rdparty/aom/include -I./conan/lib3rdparty/libvpl/include/vpl -I./conan/lib3rdparty/zlib-mt/include/ -I./conan/lib3rdparty/zimg/include/" --extra-ldflags="-libpath:./conan/lib3rdparty/videoai/lib -libpath:./conan/lib3rdparty/zlib-mt/lib -libpath:./conan/lib3rdparty/libvpx/lib -libpath:./conan/lib3rdparty/aom/lib -libpath:./conan/lib3rdparty/libvpl/lib -libpath:./conan/lib3rdparty/zimg/lib -incremental:no"
elif [[ "${ARCH}" == "ARM64" ]]; then
    
    ./configure --toolchain=msvc --enable-cross-compile --arch=arm64 --target-os=win32 --host-cc=cl --stdc=c17 --prefix=output-conan --enable-mediafoundation --enable-d3d11va --enable-dxva2 --enable-libvpx --enable-libaom --enable-shared --disable-asm --enable-neon --enable-nvenc --enable-nvdec --disable-vulkan --disable-amf --disable-libvpl --enable-zlib --enable-libzimg --enable-tvai --disable-doc --disable-htmlpages --disable-manpages --disable-txtpages --extra-cflags="/std:c17 -I/usr/local/include -I./conan/lib3rdparty/videoai/include/videoai -I./conan/lib3rdparty/libvpx/include -I./conan/lib3rdparty/libaom-av1/include -I./conan/lib3rdparty/zlib-mt/include/ -I./conan/lib3rdparty/zimg/include/" --extra-ldflags="-libpath:./conan/lib3rdparty/videoai/lib -libpath:./conan/lib3rdparty/zlib-mt/lib -libpath:./conan/lib3rdparty/libvpx/lib -libpath:./conan/lib3rdparty/libaom-av1/lib -libpath:./conan/lib3rdparty/zimg/lib -incremental:no"
fi

# configure silently drops nvenc if ffnvcodec is unusable, so fail loudly instead
for enc in H264 HEVC AV1; do
    grep -q "^#define CONFIG_${enc}_NVENC_ENCODER 1" config_components.h || { echo "[ERROR] ${enc} nvenc not enabled"; exit 1; }
done

if [[ "${ARCH}" == "ARM64" ]]; then
    echo "[INFO] make build"
    make -r -j$(nproc) 
    
    # Replace ARM64 bin2c with x64 version
    echo "[INFO] build_win_post.sh: Replacing ARM64 bin2c with x64 version"
    cp ffbuild/bin2c_x64.exe ffbuild/bin2c.exe
    
    # Now build normally with working bin2c
    echo "[INFO] make install"
    make -r install
else
    make clean
    make -r -j$(nproc) install
fi
