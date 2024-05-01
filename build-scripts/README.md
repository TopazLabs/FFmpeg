# Topaz Labs FFmpeg Build Scripts
These scripts will setup an FFmpeg build environment with the right options enabled for our uses.

## Requirements
- CUDA Toolkit
- Windows SDK
- MSVC >= 14.12
- Intel Graphics Driver
- MSYS2

## Setup
Start mingw64 from the Visual Studio x64 Native Tools Command Prompt, and then:
```
cd /path/to/this/repository
source build-scripts/setup-msvc-toolchain.sh
export DEPENDENCIES_DIR=/path/to/where/you/want/dependencies
bash build-scripts/install-dependencies
```
**Before running the provided configure command you should build the Intel MSDK and place libmfx.lib in `${DEPENDENCIES_DIR}/msdk-lib/`**. This step must be done manually at this time.

### Running the Configure Command
```
./configure --toolchain=msvc --enable-shared --enable-nvenc --enable-nvdec --disable-vulkan --enable-amf --enable-libmfx --enable-zlib --extra-cflags="-I${DEPENDENCIES_DIR}/nv_sdk -I${DEPENDENCIES_DIR}/include -I/mingw64/include" --extra-ldflags="-libpath:${DEPENDENCIES_DIR}/nv_sdk -libpath:${DEPENDENCIES_DIR}/msdk-lib -libpath:${DEPENDENCIES_DIR}/zlib-binary"
```
You may want to add a `--prefix=/path/to/install/FFmpeg`.

## Compiling
Run `make -r -j8` followed by a `make -r install`