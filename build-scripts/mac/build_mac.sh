#!/bin/bash

#OLD_PKG_CONFIG_PATH=$PKG_CONFIG_PATH

if [ -z "$2" -o -z "$3" -o -z "$4" ]; then
	echo "Usage: ./build_mac.sh [tvai enabled? (0 or 1)] [arm output path] [x86 output path] [universal output path] (extra cflags) (extra ldflags) (path to arm openh264 binaries) (path to x86 openh264 binaries)"
	exit 1
fi;

FLAGS=(--enable-libvpx --enable-libaom --enable-libzimg --enable-openssl --disable-ffplay --enable-shared --disable-static --disable-asm --enable-neon --disable-libxcb --disable-sdl2 --disable-xlib --extra-cflags="-I./conan_arm/include" --extra-ldflags="-L./conan_arm/lib")
XFLAGS=(--arch=x86_64 --extra-cflags="-arch x86_64 -I./conan_x64/include" --extra-ldflags="-arch x86_64 -L./conan_x64/lib" --disable-ffplay --enable-cross-compile --enable-shared --enable-libvpx --enable-libaom --enable-libzimg --enable-openssl --disable-libxcb --disable-sdl2 --disable-xlib)
if [[ "$1" -eq 1 ]]; then
	bash ./build-scripts/mac/conan_mac.sh
	CONAN_X64=./conan_x64
	CONAN_ARM=./conan_arm
	export PATH=${CONAN_X64}/bin:${CONAN_ARM}/bin:$PATH
	FLAGS=(--extra-cflags="-I${CONAN_ARM}/include/videoai -I${CONAN_ARM}/include $5" --extra-ldflags="-L${CONAN_ARM}/lib -headerpad_max_install_names $6" --enable-tvai ${FLAGS[@]})
	XFLAGS=(--arch=x86_64 --extra-cflags="-arch x86_64 -I${CONAN_X64}/include/videoai -I${CONAN_X64}/include $5" --extra-ldflags="-arch x86_64 -L${CONAN_X64}/lib -headerpad_max_install_names $6" --enable-shared --disable-static --enable-cross-compile --enable-tvai --enable-libvpx --enable-libaom --enable-libzimg --enable-openssl --disable-ffplay --disable-libxcb --disable-sdl2 --disable-xlib)
fi

echo "$2, $3, and $4 will be deleted in 10 seconds. Press control-c to abort..."
sleep 10
rm -rf $2
rm -rf $3
rm -rf $4

set -e
shopt -s extglob

#export PKG_CONFIG_PATH=$OPENH264_ARM_PKG_CONFIG_PATH:$OLD_PKG_CONFIG_PATH
export OLD_TARGET=${MACOSX_DEPLOYMENT_TARGET}
export MACOSX_DEPLOYMENT_TARGET=11.0
echo ./configure --prefix="$2" "${FLAGS[@]}"
CFLAGS="-mmacosx-version-min=11.0 -fexceptions" LDFLAGS="-mmacosx-version-min=11.0" ./configure --prefix="$2" "${FLAGS[@]}"
make clean
make -j8 install
if [ ! -z "$DO_CONAN_EXPORT" ]; then
	mkdir -p ${CONAN_PACKAGES}/prebuilt/topaz-ffmpeg/${PKG_VERSION}/macos_armv8/build_type\=Release/
	cp -Rp "$2"/* ${CONAN_PACKAGES}/prebuilt/topaz-ffmpeg/${PKG_VERSION}/macos_armv8/build_type\=Release/
fi
if [ ! -z "$CONAN_ARM" ]; then
	cp "$CONAN_ARM/lib/"*".dylib" $2/lib/
fi

#export PKG_CONFIG_PATH=$OPENH264_X86_PKG_CONFIG_PATH:$OLD_PKG_CONFIG_PATH
source ${CONAN_X64}/conanbuild.sh
export MACOSX_DEPLOYMENT_TARGET=10.14
echo ./configure --prefix="$3" "${XFLAGS[@]}"
CFLAGS="-mmacosx-version-min=10.14 -fexceptions" LDFLAGS="-mmacosx-version-min=10.14" ./configure --prefix="$3" "${XFLAGS[@]}"
make clean
make -j8 install
if [ ! -z "$DO_CONAN_EXPORT" ]; then
	mkdir -p ${CONAN_PACKAGES}/prebuilt/topaz-ffmpeg/${PKG_VERSION}/macos_x86_64/build_type\=Release/
	cp -Rp "$3"/* ${CONAN_PACKAGES}/prebuilt/topaz-ffmpeg/${PKG_VERSION}/macos_x86_64/build_type\=Release/
fi
if [ ! -z "$CONAN_X64" ]; then
	cp "$CONAN_X64/lib/"*".dylib" $3/lib/
fi

#Stop here if we're in Azure
if [ ! -z "${BUILD_REASON}" ]; then
	exit 0
fi

#export PKG_CONFIG_PATH=$OLD_PKG_CONFIG_PATH

UNIVERSAL_DIR="$4"
mkdir -p ${UNIVERSAL_DIR}
mkdir -p ${UNIVERSAL_DIR}/Frameworks

mkdir -p $2/bin/deps
echo `pwd`

shopt -s nullglob
python3 ./build-scripts/mac/copy_deps.py $2/bin/deps "$2/bin/ff"*
if [ ! -z "$2/bin/deps/"*".dylib" ]; then
	echo cp "$2/bin/deps/"*".dylib" $2/lib
	cp "$2/bin/deps/"*".dylib" $2/lib
fi
rm -rf $2/bin/deps

mkdir -p $2/lib/deps
python3 ./build-scripts/mac/copy_deps.py $2/lib/deps "$2/lib/"*".dylib"
for f in "$2/lib/deps/"*".dylib"; do
	if [ ! -f $2/lib/`basename $f` ]; then
		cp $f $2/lib/`basename $f`
	fi
done
rm -rf $2/lib/deps

mkdir -p $3/bin/deps
python3 ./build-scripts/mac/copy_deps.py $3/bin/deps "$3/bin/ff"*
if [ ! -z "$3/bin/deps/"*".dylib" ]; then
	echo cp "$3/bin/deps/"*".dylib" $3/lib
	cp "$3/bin/deps/"*".dylib" $3/lib
fi
rm -rf $3/bin/deps

mkdir -p $3/lib/deps
python3 ./build-scripts/mac/copy_deps.py $3/lib/deps "$3/lib/"*".dylib"
for f in "$3/lib/deps/"*".dylib"; do
	if [ ! -f $3/lib/`basename $f` ]; then		
		cp $f $3/lib/`basename $f`
	fi
done
rm -rf $3/lib/deps

set +e
for f in "$2/lib/"*".dylib"; do
	if [ -f $3/lib/`basename $f` ]; then
		lipo -create $f $3/lib/`basename $f` -output ${UNIVERSAL_DIR}/Frameworks/`basename $f`
	else
		cp $f ${UNIVERSAL_DIR}/Frameworks/`basename $f`
	fi
done

set -e
for f in "$3/lib/"*".dylib"; do
	if [ ! -f ${UNIVERSAL_DIR}/Frameworks/`basename $f` ]; then
		cp $f ${UNIVERSAL_DIR}/Frameworks/`basename $f`
	fi
done

mkdir -p ${UNIVERSAL_DIR}/MacOS
for exe in "$2/bin/ff"*; do
	lipo -create $exe $3/bin/`basename $exe` -output ${UNIVERSAL_DIR}/MacOS/`basename $exe`
	python3 ./build-scripts/mac/fixDeps.py ${UNIVERSAL_DIR}/MacOS/`basename $exe`
	codesign -f -s 'Developer ID Application: Topaz Labs, LLC (3G3JE37ZHF)' ${UNIVERSAL_DIR}/MacOS/`basename $exe`
done
for lib in ${UNIVERSAL_DIR}/Frameworks/*.dylib; do
	python3 ./build-scripts/mac/fixDeps.py $lib
	codesign -f -s 'Developer ID Application: Topaz Labs, LLC (3G3JE37ZHF)' $lib
done

bash build-scripts/mac/copy_qt_framework.sh $HOME/Qt/6.2.4/macos ${UNIVERSAL_DIR}
bash build-scripts/mac/package_bundle.sh ${UNIVERSAL_DIR} build-dmg "Topaz Video Enhance AI 3"

export MACOSX_DEPLOYMENT_TARGET=${OLD_TARGET}
