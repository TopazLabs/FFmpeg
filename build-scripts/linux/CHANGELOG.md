## 2026-03-19

- Added `BUILD_LINUX_GPL` handling in `conan_linux.sh` to pass Conan option `build_linux_GPL=True/False`.
- GPL-enabled builds now install Conan packages for `libx264` and `libx265` through existing `conanfile.py` option logic.
