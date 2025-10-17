git apply ./build-scripts/configure-zimg.patch
git apply ./build-scripts/configure-aom.patch
echo ./configure --enable-shared --disable-ffplay --disable-libxcb --disable-vulkan --disable-sdl2 --disable-xlib --enable-tvai --enable-libvpx --enable-libaom --enable-libzimg --enable-nvenc --enable-nvdec --extra-cflags="-I./conan/include -I./conan/include/videoai" --extra-ldflags="-Wl,-rpath,./conan/lib/ -L./conan/lib" --prefix=./output-conan/
set -e
./configure --enable-shared --disable-ffplay --disable-libxcb --disable-vulkan --disable-sdl2 --disable-xlib --enable-tvai --enable-libvpx --enable-libaom --enable-libzimg --enable-nvenc --enable-nvdec --extra-cflags="-I./conan/include -I./conan/include/videoai" --extra-ldflags="-Wl,-rpath,./conan/lib/ -L./conan/lib" --prefix=./output-conan/

make clean
make -j$(nproc) install

# mkdir -p $(Build.SourcesDirectory)/topaz-conan-dev/prebuilt/topaz-ffmpeg/$(VERSION)/linux_x86_64/build_type\=Release/
# cp build-scripts/deploy_conanfile.py $(Build.SourcesDirectory)/topaz-conan-dev/prebuilt/topaz-ffmpeg/$(VERSION)/conanfile.py
# cp -Rp "output-conan"/* $(Build.SourcesDirectory)/topaz-conan-dev/prebuilt/topaz-ffmpeg/$(VERSION)/linux_x86_64/build_type\=Release/
# cd $(Build.SourcesDirectory)/topaz-conan-dev
# bash ./run_publish_prebuilt.sh --package-name topaz-ffmpeg --package-version $(VERSION) -r topaz-conan