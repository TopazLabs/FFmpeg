#!/bin/bash

if [[ -z "$CUDA_PATH" ]]; then
	echo "CUDA_PATH is not set. nvenc will not be available."
fi
if [[ -z "$VCINSTALLDIR" ]]; then
	echo "Couldn't find Visual Studio install location. Aborting."
	return 1
fi
if [[ -z "$WindowsSdkVerBinPath" ]]; then
	echo "WindowsSdkVerBinPath is not set. Aborting."
	return 1
fi

# Convert Windows paths to Unix-style paths
VCINSTALLDIR_UNIX=$(cygpath -u "$VCINSTALLDIR")
WindowsSdkVerBinPath_UNIX=$(cygpath -u "$WindowsSdkVerBinPath")
CUDA_PATH_UNIX=$(cygpath -u "$CUDA_PATH")

export PATH="${VCINSTALLDIR_UNIX}/Tools/MSVC/${VCToolsVersion}/bin/Hostx64/x64/":$PATH
export PATH="${VCINSTALLDIR_UNIX}/../MSBuild/Current/Bin":$PATH
if [[ ! -z "$CUDA_PATH" ]]; then
	export PATH="${CUDA_PATH_UNIX}/bin/":$PATH
fi
export PATH="${WindowsSdkVerBinPath_UNIX}/x64/":$PATH
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig":$PKG_CONFIG_PATH
