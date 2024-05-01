#!/bin/bash

BASE_FOLDER=$1
OUTPUT_FOLDER=$2
OUTPUT_NAME=$3

rm -rf ${OUTPUT_FOLDER}
mkdir -p "${OUTPUT_FOLDER}/${OUTPUT_NAME}".app/Contents
cp -Rp "${BASE_FOLDER}/MacOS" "${OUTPUT_FOLDER}/${OUTPUT_NAME}".app/Contents/
cp -Rp "${BASE_FOLDER}/Frameworks" "${OUTPUT_FOLDER}/${OUTPUT_NAME}".app/Contents/
cp build-scripts/mac/Info.plist "${OUTPUT_FOLDER}/${OUTPUT_NAME}".app/Contents/
