## 2026-11-19

- `conan_linux.sh` takes the Conan profile as a third argument (`profile_ubuntu24.04` by default, or `profile_ubuntu22.04`).
- Azure: `linux_2204` (UbuntuVMSS, gcc-11) and `linux_2404` (LinuxManaged, gcc-14) choose the Ubuntu versions. `linux` / `linux_tensorrt_rtx` choose the Enterprise / RTX variants built on each one.
- 22.04 and 24.04 builds of one VERSION get the same topaz-ffmpeg package id, so the 22.04 jobs fail at their first step when publish is checked, instead of replacing the 24.04 binary.

## 2026-03-19

- Added `BUILD_LINUX_GPL` handling in `conan_linux.sh` to pass Conan option `build_linux_GPL=True/False`.
- GPL-enabled builds now install Conan packages for `libx264` and `libx265` through existing `conanfile.py` option logic.
