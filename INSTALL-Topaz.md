## Installing FFmpeg for Topaz Labs Products

### Mac
You should install the dependencies mentioned [here](https://trac.ffmpeg.org/wiki/CompilationGuide/macOS#CompilingFFmpegyourself). We likely won't use all of them, but you'll get everything you need.

1. If you wish to enable VEAI support, locate the veai.h file and the libveai.dylib and libaiengine.dylib files included with VEAI. Make a note of these two folders.

2. Execute the following command:
```
./configure --prefix=/path/to/install/to --enable-shared --enable-veai --extra-cflags="-I/path/to/folder/with/veai.h" --extra-ldflags="-L/path/to/folder/with/libveai.dylib"
```

If there are additional features you want enabled, make sure you add the corresponding option(s) to the above command.

3. Execute the following commands:
```
make -j8 install
```

FFmpeg will be installed to /path/to/install/to.

4. If the executables complain about missing dylibs, you may need to use install_name_tool to change the path to one or more dependencies.
```
otool -L /path/to/executable
-OUTPUT SNIPPED-
libveai.0.dylib ...
-OUTPUT SNIPPED-

install_name_tool -change libveai.0.dylib /path/to/real/libveai.0.dylib /path/to/executable
```

You may need to repeat step 4 for multiple dependencies and files. If you're unable to find a dependency, check inside VEAI's .app file.

### Windows

You will need the following dependencies to build on Windows:
- Windows SDK
- Visual Studio 2017 or newer
- MSVC >= v14.12
- MSYS2

You may also find it helpful to have the following:
- Intel Graphics Driver (for QSV support)
- CUDA Toolkit (for nvenc/nvdec support)

1. Install the required dependencies
  - When installing MSYS2, make sure you install pkg-config instead of pkgconf

2. Open a Visual Studio x64 Native Tools Command Prompt
  - Step 3 will be executed in this Command Prompt

3. Navigate to your MSYS2 install and run mingw64
  - Future steps will be run in the window that this opens

4. Navigate to this repository

5. Execute `source build-scripts/setup-msvc-toolchain.sh`

6. Determine where you wish for additional dependencies to be installed. Then execute `export DEPENDENCIES_DIR=/path/to/place/dependencies`

7. Run `bash build-scripts/install-dependencies.sh`
  - This will install additional libraries needed to build with our desired feature set. You *MUST* already have the dependencies at the top of this document properly installed.

8. Ensure that libmfx.lib is in `${DEPENDENCIES_DIR}/msdk-lib`. If it is not present, build the Intel Media SDK Dispatcher manually.

9. If you wish to enable VEAI support you will need to find the veai.h, aiengine.lib, and veai.lib files provided with VEAI. Make a note of what folders these are in.

10. Determine which features you will enable. We use all of the following:
  - `--enable-nvenc` CUDA encoders
  - `--enable-nvdec` CUDA decoders
  - `--enable-amf` AMD Advanced Meda Framework
  - `--enable-libmfx` Intel Media SDK
  - `--enable-veai` Enable our VEAI filters
  - `--enable-zlib` zlib, required for some encoders/decoders
  - `--disable-vulkan` Disables Vulkan support. *At the time of writing, this is required*

11. In the root of this repository, run the configure command with your options. We use the following command to enable all our desired features:
```
./configure --toolchain=msvc --prefix=/path/to/install/to --enable-shared --enable-nvenc --enable-nvdec --disable-vulkan \
 --enable-amf --enable-libmfx --enable-zlib --enable-veai \ 
 --extra-cflags="-I${DEPENDENCIES_DIR}/nv_sdk -I${DEPENDENCIES_DIR}/include -I/mingw64/include -I/path/to/folder/with/veai.h" \
 --extra-ldflags="-libpath:${DEPENDENCIES_DIR}/nv_sdk -libpath:${DEPENDENCIES_DIR}/msdk-lib -libpath:${DEPENDENCIES_DIR}/zlib-binary -libpath:/path/to/folder/with/veai.lib" \
```

If you wish to enable another feature, or disable a specific feature, make sure to add the corresponding option to the command

12. Run the following:
```
make -r -j8
make -r install
```

13. ffmpeg is now installed to `/path/to/install/to`. You will likely need to copy the dll files from your VEAI install into the `/path/to/install/to/bin` folder to run the program.
