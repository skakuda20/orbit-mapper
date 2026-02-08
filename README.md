# orbit-mapper

**orbit-mapper** is a Qt + OpenGL desktop app for visualizing satellite orbits.

## Quick Start (Linux)

1. **Run the setup script:**

	 ```bash
	 ./scripts/setup.sh
	 ```
	 - This installs all required dependencies (Qt, CMake, build tools) and builds the app.
	 - By default, it uses Qt5. To use Qt6, run:
		 ```bash
		 QT_VERSION=6 ./scripts/setup.sh
		 ```

2. **Run the app:**

	 ```bash
	 ./build/orbit_mapper
	 ```

## Using the App

- **Rotate camera:** Left-drag with mouse
- **Zoom:** Mouse wheel
- **Add satellites:** Use the "Add Satellite" button in the side panel
- **Edit orbits:** Adjust orbital elements with sliders/spinboxes
- **Simulation speed:** Use the bottom bar to pause or change time scale

## Notes

- If you use Snap-installed VS Code and see a `libpthread.so.0` error, try running with:
	```bash
	env -u LD_LIBRARY_PATH -u LD_PRELOAD -u SNAP -u SNAP_NAME -u SNAP_REVISION -u SNAP_VERSION ./build/orbit_mapper
	```

## SGP4 Integration

The app includes a stub SGP4 propagator. To use a real SGP4 library, see comments in `src/orbit/Sgp4Propagator.cpp` and the CMake options in `CMakeLists.txt`.