#!/bin/bash

set -e

# Parse command-line arguments
# --icx: Use Intel oneAPI ICX compiler
# --no-warnings: Disable warnings
USE_ICX=false
DISABLE_WARNINGS=""
for arg in "$@"; do
  case $arg in
    --icx)
      USE_ICX=true
      shift
      ;;
    --no-warnings)
      DISABLE_WARNINGS="-w"
      shift
      ;;
    *)
      ;;
  esac
done

# Ensure all expected system dependencies
pacman -S --noconfirm base-devel

# Navigate to sources directory
cd "$(dirname "$0")/../../../"

# Install nv-codec-headers (if present in monorepo layout)
cd "./nv-codec-headers"
make install

# Navigate to FFmpeg directory (from nv-codec-headers directory)
cd "../FFmpeg"

# Set compiler based on --icx flag
if [ "$USE_ICX" = true ]; then
  export USE_ICX_TOOLCHAIN=1
  CC_COMPILER="icx"
  CXX_COMPILER="icpx"
else
  CC_COMPILER=""
  CXX_COMPILER=""
fi

# Prepare environment (MSVC or ICX)
source ./build-scripts/win/setup-msvc-toolchain.sh

# Build configure command
CONFIGURE_CMD="./configure --toolchain=msvc --prefix=output-conan"

# Add compiler flags if using ICX
if [ "$USE_ICX" = true ]; then
  CONFIGURE_CMD="$CONFIGURE_CMD --cc=$CC_COMPILER --cxx=$CXX_COMPILER"
fi

# Add common configuration options
CONFIGURE_CMD="$CONFIGURE_CMD \
  --enable-libvpx \
  --enable-libaom \
  --enable-shared \
  --enable-x86asm --x86asmexe=nasm \
  --enable-nvenc --enable-nvdec \
  --disable-vulkan \
  --enable-amf \
  --enable-libvpl \
  --enable-zlib \
  --enable-libzimg \
  --enable-tvai \
  --extra-cflags=\"${DISABLE_WARNINGS} -I./conan/lib3rdparty/videoai/include/videoai -I./conan/lib3rdparty/amf/include -I./conan/lib3rdparty/libvpx/include -I./conan/lib3rdparty/libaom-av1/include -I./conan/lib3rdparty/libvpl/include/vpl -I./conan/lib3rdparty/zlib-mt/include/ -I./conan/lib3rdparty/zimg/include/\" \
  --extra-ldflags=\"-libpath:./conan/lib3rdparty/videoai/lib -libpath:./conan/lib3rdparty/zlib-mt/lib -libpath:./conan/lib3rdparty/libvpx/lib -libpath:./conan/lib3rdparty/libaom-av1/lib -libpath:./conan/lib3rdparty/libvpl/lib -libpath:./conan/lib3rdparty/zimg/lib -incremental:no\""

# Execute configure
eval $CONFIGURE_CMD

make clean
make -r -j$(nproc) install
