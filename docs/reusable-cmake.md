# Reusing the portable libraries with CMake

CMake 3.16+, a C11 compiler and a C++17 compiler are required. Configure either
[core](../core/CMakeLists.txt) or [scene](../platform/scene/CMakeLists.txt) directly,
or add either directory from a parent project. The scene entry point includes
core and the renderer when their targets do not already exist.

| Setting | Standalone default | Embedded default | Purpose |
| --- | --- | --- | --- |
| `NNN_USE_SYSTEM_DEPENDENCIES` | `OFF` | `OFF` | Use installed zlib/libpng packages instead of the pinned sources. |
| `NNN_BUILD_TOOLS` | `ON` | `OFF` | Build script/image/archive/PNG probes, audio check and scene player. |
| `BUILD_TESTING` | CTest default (`ON`) | Parent-controlled | Build the existing fixture tests. |
| `NNN_ENABLE_SANITIZERS` | `OFF` | `OFF` | GCC/Clang ASan and UBSan for core targets. |

Embedded directories do not include CTest, create `BUILD_TESTING` or force its
value. A parent that wants tests should call `include(CTest)` or
`enable_testing()` itself and select `BUILD_TESTING`. Tools and tests are
independent options. Android continues to omit the desktop audio check and scene
player even when tools are requested. Other probes can be explicitly enabled for
cross-compilation.

## Standalone builds

Bundled mode remains the default and requires no host zlib/libpng development
packages. Their upstream sources and build configuration are retained.

```sh
cmake -S platform/scene -B build/scene -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/scene --config Debug --parallel 4
ctest --test-dir build/scene -C Debug --output-on-failure
```

Use a separate build directory for installed dependencies:

```sh
cmake -S platform/scene -B build/scene-system -DCMAKE_BUILD_TYPE=Debug \
  -DNNN_USE_SYSTEM_DEPENDENCIES=ON -DBUILD_TESTING=ON -DNNN_BUILD_TOOLS=ON
cmake --build build/scene-system --config Debug --parallel 4
ctest --test-dir build/scene-system -C Debug --output-on-failure
```

This mode calls `find_package(ZLIB REQUIRED)` and `find_package(PNG REQUIRED)`,
links `ZLIB::ZLIB` and `PNG::PNG`, and leaves the parent PNG/ZLIB build
options and CMake module path alone. The scene player uses `PNG::PNG` in
both modes. Package lookup honors normal CMake settings such as
`CMAKE_PREFIX_PATH`, `ZLIB_ROOT` and a package-manager toolchain.
Bundled mode configures its private upstream options and supplies the canonical
targets as facades for the pinned libraries; use system mode when a parent already
provides zlib/libpng targets. Avoid switching dependency providers in an existing
build tree, whose cached upstream options may remain from a previous configure.

On Windows, use a recent Visual Studio 2022 C/C++ toolchain (including C11 support).
For system dependencies, one example is vcpkg's `libpng:x64-windows` package
(which includes zlib) with `CMAKE_TOOLCHAIN_FILE` pointing to its CMake toolchain
and `VCPKG_TARGET_TRIPLET=x64-windows`. Match package architecture, configuration
and C runtime linkage to the consumer. Shared package DLLs must be on the test or
application loader path. On macOS, Homebrew zlib/libpng prefixes can be passed in
`CMAKE_PREFIX_PATH`.

## A shared Aether consumer

```cmake
cmake_minimum_required(VERSION 3.16)
project(aether LANGUAGES C CXX)

set(NNN_USE_SYSTEM_DEPENDENCIES ON CACHE BOOL "Use Aether dependency packages")
set(NNN_BUILD_TOOLS OFF CACHE BOOL "Omit diagnostic programs")
# Omit include(CTest) when the parent does not want fixture tests.
add_subdirectory(external/SystemNNN-Emberfall/platform/scene nnn)

add_library(aether SHARED aether_bridge.cpp)
target_link_libraries(aether PRIVATE emberfall-scene)
```

The reusable targets are `nnn-script-core`, `nnn-game-audio`,
`nnn-image-core`, `nnn-archive-core`, `nnn-png-core`,
`nnn-dwq-core`, `emberfall-render` and `emberfall-scene`. Their public
include directories, C++17 requirement and link dependencies propagate through
CMake targets. All project static libraries and the bundled zlib/libpng archives
explicitly enable position-independent code, even if the parent default is OFF.
Installed static packages must themselves have been built with PIC when the target
platform requires it; this project cannot recompile imported archives. These are
build-tree targets, not an installed/exported SDK package.

## Validation and CI

The [portable CMake workflow](../.github/workflows/cmake.yml) builds core and scene
with bundled and installed dependencies on Linux, Windows/MSVC and macOS, including
diagnostic programs and host fixture tests. Its Linux embedding jobs check the
embedded defaults, parent option preservation and link every static-library object
into a shared consumer. Windows/macOS jobs are CI coverage; a Linux development
run does not establish that those platform jobs have passed.

The optional Android dispatch compiles scene/core/render for arm64-v8a using the
runner's Android NDK, bundled dependencies, and tools/tests disabled. It does not
build the JNI player or FFmpeg, install an APK, or run device tests. The optional
`emberfall-android-jvm` CTest needs a JDK, Android SDK and installed game data;
the portable workflow excludes it explicitly. Existing C++ fixture tests and the
Python configuration audit use repository data.
