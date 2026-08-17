#!/bin/bash

if [[ -z "$CUDA_PATH" ]]; then
    echo "CUDA_PATH is not set. nvenc will not be available."
else
    CUDA_PATH_UNIX=$(cygpath -u "$CUDA_PATH")
    export PATH="${CUDA_PATH_UNIX}/bin/":$PATH
fi

if [[ -z "$VCINSTALLDIR" ]]; then
    echo "Couldn't find Visual Studio install location. Aborting."
    return 1
else
    VCINSTALLDIR_UNIX=$(cygpath -u "$VCINSTALLDIR")
    export PATH="${VCINSTALLDIR_UNIX}/Tools/MSVC/${VCToolsVersion}/bin/Hostx64/x64/":$PATH
    export PATH="${VCINSTALLDIR_UNIX}/../MSBuild/Current/Bin":$PATH
fi

if [[ -z "$WindowsSdkVerBinPath" ]]; then
    echo "WindowsSdkVerBinPath is not set. Aborting."
    return 1
else
    WindowsSdkVerBinPath_UNIX=$(cygpath -u "$WindowsSdkVerBinPath")
    export PATH="${WindowsSdkVerBinPath_UNIX}/x64/":$PATH
fi

export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig":$PKG_CONFIG_PATH

# Add yasm to PATH
#  --> changed to nasm because lost support for yasm
SCRIPT_DIR_UNIX=$(cygpath -u "$(dirname "$0")")
export PATH="${SCRIPT_DIR_UNIX}/../../conan/lib3rdparty/nasm/bin":$PATH
