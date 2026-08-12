# bat-img-cpp

`bat-img-cpp` is a cross-platform C++ batch image-processing command-line tool,
packaged as a Python wheel. It uses OpenCV for decoding, transformations, and
encoding, and uses Exiv2 to remove metadata.

## Supported platforms

- macOS (Apple Silicon and Intel)
- Linux (x86_64 and ARM64)
- Windows (x86_64)

Each release needs a wheel built on (or cross-compiled for) the target platform.
The native executable is installed inside the wheel, while Python only provides
the `bat_img` console-script launcher.

## Build prerequisites

- CMake 3.21+
- C++17 compiler
- OpenCV development files, including `core`, `imgcodecs`, and `imgproc`
- Python 3.9+
- Exiv2 development files for `--strip-gps` and `--strip-all`

Typical dependencies:

```bash
# macOS
brew install cmake opencv exiv2

# Ubuntu/Debian
sudo apt install cmake g++ libopencv-dev libexiv2-dev

# Windows (vcpkg)
vcpkg install opencv4 exiv2
```

## Build and run locally

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/bat_img --help
```

Build a wheel:

```bash
python -m pip install build
python -m build
python -m pip install dist/*.whl
bat_img --input ./photos --recursive --resize 1920x0 --format webp --output ./out
```

## HEIC

HEIC is deliberately dependent on the OpenCV build's installed codec backend.
It is not guaranteed by the base wheel. For portable HEIC wheels, build OpenCV
with libheif support and bundle the resulting dynamic libraries in each target
wheel. The base tool reports a normal decode error if its OpenCV build cannot
decode a HEIC image.

## Release matrix

Build and test wheels for these targets in CI:

| Target | Suggested runner |
| --- | --- |
| `macosx_11_0_arm64` | macOS Apple Silicon |
| `macosx_10_15_x86_64` | macOS Intel |
| `manylinux_2_28_x86_64` | manylinux container |
| `manylinux_2_28_aarch64` | ARM64 manylinux container |
| `win_amd64` | Windows runner |

Use `delocate` on macOS, `auditwheel` on Linux, and `delvewheel` on Windows to
bundle non-system dynamic libraries before publishing a wheel.
