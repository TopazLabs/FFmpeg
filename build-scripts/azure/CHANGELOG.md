## 2026-03-19

- Passed `BUILD_LINUX_GPL` from the Ubuntu Azure pipeline step into the Conan install script environment.
- Made Linux FFmpeg `./configure` flags conditional so GPL builds add `--enable-gpl --enable-libx264 --enable-libx265` and normal builds keep the non-GPL flags.
