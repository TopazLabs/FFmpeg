#!/bin/bash

VERSION=$1

set -e

USER="$2"
CHANNEL="$3"

TOPAZ_CONAN=$4

VEAI_ENABLED=$5

cd ../..
DO_CONAN_EXPORT=1 CONAN_PACKAGES=${TOPAZ_CONAN} PKG_VERSION=${VERSION} bash build-scripts/mac/build_mac.sh $VEAI_ENABLED ./builds-arm ./builds-x86 ./builds-univ '' ''
cp build-scripts/deploy_conanfile.py ${TOPAZ_CONAN}/prebuilt/topaz-ffmpeg/${VERSION}/conanfile.py


cd ${TOPAZ_CONAN}
bash ./run_publish_prebuilt.sh --package-name topaz-ffmpeg --package-version ${VERSION} -r topaz-conan
