set -e

# install conan deps
build-scripts/linux/conan_linux.sh

apply_configure_patch() {
    patch_file=$1

    if git apply --check "$patch_file" >/dev/null 2>&1; then
        git apply "$patch_file"
    elif git apply --reverse --check "$patch_file" >/dev/null 2>&1; then
        echo "Skipping already-applied patch: $patch_file"
    else
        git apply "$patch_file"
    fi
}

apply_configure_patch ./build-scripts/configure-zimg.patch
apply_configure_patch ./build-scripts/configure-aom.patch
apply_configure_patch ./build-scripts/configure-dav1d.patch
echo ./configure --enable-shared --disable-ffplay --disable-libxcb --disable-vulkan --disable-sdl2 --disable-xlib --enable-tvai --enable-libaom --enable-libzimg --enable-nvenc --enable-nvdec --enable-libdav1d --extra-cflags="-I./conan/include -I./conan/include/videoai" --extra-ldflags="-Wl,-rpath,./conan/lib/ -L./conan/lib" --prefix=./output-conan/
./configure --enable-shared --disable-ffplay --disable-libxcb --disable-vulkan --disable-sdl2 --disable-xlib --enable-tvai --enable-libaom --enable-libzimg --enable-nvenc --enable-nvdec --enable-libdav1d --extra-cflags="-I./conan/include -I./conan/include/videoai" --extra-ldflags="-Wl,-rpath,./conan/lib/ -L./conan/lib" --prefix=./output-conan/

make clean
make -j$(nproc) install

# mkdir -p $(Build.SourcesDirectory)/topaz-conan-dev/prebuilt/topaz-ffmpeg/$(VERSION)/linux_x86_64/build_type\=Release/
# cp build-scripts/deploy_conanfile.py $(Build.SourcesDirectory)/topaz-conan-dev/prebuilt/topaz-ffmpeg/$(VERSION)/conanfile.py
# cp -Rp "output-conan"/* $(Build.SourcesDirectory)/topaz-conan-dev/prebuilt/topaz-ffmpeg/$(VERSION)/linux_x86_64/build_type\=Release/
# cd $(Build.SourcesDirectory)/topaz-conan-dev
# bash ./run_publish_prebuilt.sh --package-name topaz-ffmpeg --package-version $(VERSION) -r topaz-conan