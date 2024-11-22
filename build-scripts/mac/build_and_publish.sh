#!/bin/bash

VERSION=$1

set -e

USER="$2"
CHANNEL="$3"
VEAI_ENABLED=$4

cd ../..
DO_CONAN_EXPORT=1 PKG_VERSION=${VERSION} bash build-scripts/mac/build_mac.sh $VEAI_ENABLED ./builds-arm ./builds-x86 ./builds-univ '' ''

cd build-scripts
conan export-pkg deploy_conanfile.py \
    --name topaz-ffmpeg --version "$VERSION" \
    -pr:b profile_mac_armv8 -pr:h profile_mac_armv8 \
    -s build_type=Release -c user.profile_name=../builds-arm
conan export-pkg deploy_conanfile.py \
    --name topaz-ffmpeg --version "$VERSION" \
    -pr:b profile_mac_x86_64 -pr:h profile_mac_x86_64 \
    -s build_type=Release -c user.profile_name=../builds-x86
conan upload "topaz-ffmpeg/$VERSION" -r topaz-conan
