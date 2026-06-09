## 2026-06-09

- Simplified Linux FFmpeg configure flags so x86_64-only libvpx support is explicit.
- Fixed Linux ARM64 Azure setup to install cross-compile packages and configure FFmpeg with the aarch64 toolchain.
- Trimmed Linux ARM64 Azure apt setup to the required aarch64 cross toolchain.
- Added a Conan library rpath-link for Linux ARM64 configure checks.
