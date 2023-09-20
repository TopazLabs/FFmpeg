#!/bin/bash
#This assumes git bash is being used. Some changes may be needed for msys/others

BUILD_LOCATION="$1"
VERSION="$2"
USER="$3"
CHANNEL="$4"

PUBLISH_LOCATION=../../../topaz-conan-dev
if [[ ! -z "$5" ]]; then
	PUBLISH_LOCATION="$5"
fi

cd ../../
mkdir -p ${PUBLISH_LOCATION}/prebuilt/topaz-ffmpeg/${VERSION}/profile_win2019/build_type\=Release/
cp build-scripts/deploy_conanfile.py ${PUBLISH_LOCATION}/prebuilt/topaz-ffmpeg/${VERSION}/conanfile.py
cp -Rp ${BUILD_LOCATION}/. ${PUBLISH_LOCATION}/prebuilt/topaz-ffmpeg/${VERSION}/profile_win2019/build_type\=Release/

cd ${PUBLISH_LOCATION}
cmd //c "CALL run_publish_prebuilt.cmd --package-name topaz-ffmpeg --package-version ${VERSION} -r topaz-conan"
