#!/bin/bash

# This script prepares the environment to build FFmpeg on Windows using
# either MSVC (default) or Intel oneAPI ICX/ICPX in MSVC (clang-cl) mode.
#
# For ICX mode, prerequisites (must be done BEFORE launching MSYS2):
#   1. Run Intel oneAPI setvars.bat in cmd.exe/PowerShell
#   2. Launch MSYS2 with -use-full-path to inherit PATH
#
# Example:
#   "C:\Program Files (x86)\Intel\oneAPI\setvars.bat"
#   C:\msys64\msys2_shell.cmd -defterm -no-start -use-full-path -mingw64 -c "bash /c/Users/.../build_win.sh --icx --no-warnings"
#
# Notes:
# - FFmpeg should be configured with --toolchain=msvc and CC/CXX set to icx/icpx for ICX.
# - Do NOT use --toolchain=icl with icx; that's for the deprecated classic ICL.

if [[ -z "$CUDA_PATH" ]]; then
    echo "CUDA_PATH is not set. nvenc will not be available."
else
    CUDA_PATH_UNIX=$(cygpath -u "$CUDA_PATH")
    export PATH="${CUDA_PATH_UNIX}/bin/":$PATH
fi

# MSVC/SDK paths are required for link.exe, headers, libs, etc.
if [[ -z "$VCINSTALLDIR" ]]; then
    echo "Couldn't find Visual Studio install location (VCINSTALLDIR). Aborting."
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

# Check for Intel oneAPI ICX if using ICX toolchain
if [[ "$USE_ICX_TOOLCHAIN" == "1" ]]; then
    if ! command -v icx &> /dev/null; then
        echo "ERROR: Intel oneAPI ICX (icx) not found in PATH."
        echo ""
        echo "Make sure you:"
        echo "  1. Run setvars.bat in cmd.exe/PowerShell BEFORE launching MSYS2"
        echo "  2. Launch MSYS2 with -use-full-path flag to inherit PATH"
        echo ""
        echo "Example:"
        echo '  "C:\Program Files (x86)\Intel\oneAPI\setvars.bat"'
        echo '  C:\msys64\msys2_shell.cmd -defterm -no-start -use-full-path -mingw64'
        return 1
    fi
    echo "Intel oneAPI ICX found: $(which icx)"
fi

export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig":$PKG_CONFIG_PATH

# Add nasm to PATH (used for x86 asm)
SCRIPT_DIR_UNIX=$(cygpath -u "$(dirname "$0")")
export PATH="${SCRIPT_DIR_UNIX}/../../conan/lib3rdparty/nasm/bin":$PATH
