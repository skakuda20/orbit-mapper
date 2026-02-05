# orbit-mapper

Qt + OpenGL desktop app scaffold for visualizing satellite orbits.

Currently included:
- Qt6 `QOpenGLWidget` renderer with a demo orbit polyline
- Classical orbital elements (Keplerian) → sampled 3D polyline
- SGP4 integration hook (CMake option + wrapper) with a stub implementation until you select a specific SGP4 library

## Build (Linux)

Prereqs (Ubuntu/Debian names may vary):
- CMake >= 3.22

- A C++20 compiler (GCC/Clang)
- Qt development packages (Qt6 preferred)

If CMake can’t find Qt, either install the dev packages or point CMake at your Qt SDK via `CMAKE_PREFIX_PATH` (or `Qt6_DIR`).

Configure + build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

If you want to build with Qt5 instead:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DORBIT_MAPPER_QT_VERSION=5
cmake --build build -j
```

If you installed Qt from the official SDK, you may need:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
```

Run:

```bash
./build/orbit_mapper
```

> Note (Snap VS Code): If you run from the integrated terminal of a Snap-installed VS Code, you may see:
> `symbol lookup error ... libpthread.so.0: undefined symbol: __libc_pthread_init, version GLIBC_PRIVATE`
> This is caused by Snap runtime libraries leaking into `LD_LIBRARY_PATH`.
> Fix by using a non-snap VS Code install (recommended) or run with a clean environment:
>
> ```bash
> env -u LD_LIBRARY_PATH -u LD_PRELOAD -u SNAP -u SNAP_NAME -u SNAP_REVISION -u SNAP_VERSION ./build/orbit_mapper
> ```

Controls:
- Left-drag: rotate camera
- Mouse wheel: zoom

## SGP4 integration

The project includes a wrapper class:
- [src/orbit/Sgp4Propagator.h](src/orbit/Sgp4Propagator.h)

And a CMake hook:
- [CMakeLists.txt](CMakeLists.txt)

By default it builds a stub propagator because no SGP4 repo is selected.

To wire a real SGP4 implementation in, set these cache variables and update the link target name in `CMakeLists.txt`:

```bash
cmake -S . -B build \
	-DORBIT_MAPPER_ENABLE_SGP4=ON \
	-DORBIT_MAPPER_SGP4_REPO=https://github.com/<owner>/<repo>.git \
	-DORBIT_MAPPER_SGP4_TAG=<tag-or-commit>
```

Then replace the stub section in [src/orbit/Sgp4Propagator.cpp](src/orbit/Sgp4Propagator.cpp) with calls into the chosen library.

## Project layout

- [src/app](src/app): Qt main window
- [src/gl](src/gl): OpenGL widget + drawing
- [src/orbit](src/orbit): Orbital element math + propagation interfaces


-------
Add script to install necessary dependencies like Qt5