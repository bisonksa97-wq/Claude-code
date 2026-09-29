# Building

## Requirements

- CMake ≥ 3.21 and a C++20 compiler (GCC 11+, Clang 14+, MSVC 19.30+)
- FFmpeg ≥ 5.1 development libraries (libavformat, libavcodec, libavutil, libswscale, libswresample), found through pkg-config
- nlohmann-json ≥ 3.10
- Qt 6 (Widgets, Test) for the desktop app. Optional: without it only the engine, CLI and non-UI tests build.
- Qt 6 Multimedia for audio playback. Optional: without it the app plays silently on a wall clock.
- GoogleTest for tests

### Ubuntu / Debian
```bash
sudo apt install build-essential cmake ninja-build pkg-config \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
  nlohmann-json3-dev libgtest-dev qt6-base-dev qt6-multimedia-dev libgl-dev
```

### macOS (Homebrew)
```bash
brew install cmake ninja pkg-config ffmpeg nlohmann-json googletest qt
```

### Windows (vcpkg)
```powershell
vcpkg install ffmpeg[avcodec,avformat,swscale,swresample,x264] nlohmann-json gtest qtbase qtmultimedia pkgconf
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
```
Only Linux has been built and tested so far. The macOS and Windows recipes are untested starting points.

## Configure, build, test
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Options: `-DUP_BUILD_UI=OFF`, `-DUP_BUILD_TESTS=OFF`, `-DUP_WARNINGS_AS_ERRORS=ON`.

Outputs:
- `build/src/ui/ultimatepost-studio [project.uproj]`: desktop app
- `build/src/cli/ultimatepost`: CLI
