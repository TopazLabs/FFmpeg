## 2026-11-19

- `conan_linux.sh` takes the Conan profile as a third argument (`profile_ubuntu24.04` by default, or `profile_ubuntu22.04`).
- Azure: four checkboxes, one per Linux job: `ubuntu2404_enterprise` (default on) / `ubuntu2404_rtx` build on LinuxManaged (Ubuntu 24.04, gcc-14, jobs `Ubuntu24_04` / `Ubuntu24_04_TensorRT_RTX`); `ubuntu2204_enterprise` / `ubuntu2204_rtx` build on UbuntuVMSS (Ubuntu 22.04, gcc-11, jobs `Ubuntu22_04` / `Ubuntu22_04_TensorRT_RTX`). They replace the earlier `linux_2204` / `linux_2404` / `linux` / `linux_tensorrt_rtx` parameters.
- 22.04 and 24.04 builds of one VERSION get the same topaz-ffmpeg package id, so the 22.04 jobs fail at their first step when publish is checked, instead of replacing the 24.04 binary.

## 2026-03-19

- Added `BUILD_LINUX_GPL` handling in `conan_linux.sh` to pass Conan option `build_linux_GPL=True/False`.
- GPL-enabled builds now install Conan packages for `libx264` and `libx265` through existing `conanfile.py` option logic.
