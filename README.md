# bat-img-cpp

`bat-img-cpp` is a cross-platform C++ batch image-processing command-line tool,
packaged as a Python wheel. It uses OpenCV for decoding, transformations, and
encoding, and uses Exiv2 to remove metadata.

## Supported platforms

- macOS (Apple Silicon)
- Linux (x86_64 and ARM64)
- Windows (x86_64)

Each release needs a wheel built on (or cross-compiled for) the target platform.
The native executable is installed inside the wheel, while Python only provides
the `bat_img` console-script launcher.

## Build prerequisites

- CMake 3.21+
- C++17 compiler
- OpenCV development files, including `core`, `imgcodecs`, and `imgproc`
- libheif development files for HEIC input and output
- Python 3.9+
- Exiv2 development files for `--strip-gps` and `--strip-all`

Typical dependencies:

```bash
# macOS
brew install cmake opencv exiv2 libheif

# Ubuntu/Debian
sudo apt install cmake g++ libopencv-dev libexiv2-dev libheif-dev

# Windows (vcpkg)
vcpkg install opencv4 exiv2 libheif
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

## Make targets

```bash
make build-debug     # configure and build Debug
make test            # build Debug and run CTest
make build-release   # configure and build Release
make wheel           # build a release PEP 517 wheel in dist/
make wheel-check     # install the wheel in a temporary venv and run bat_img --help
make clean           # remove build/ and dist/
```

## HEIC

HEIC and HEIF input/output use libheif directly, independent of OpenCV's codec
plugins. libheif needs an HEVC encoder (for example x265) to write `.heic`.
The CTest suite performs an end-to-end JPEG → HEIC → PNG conversion when
`BAT_IMG_ENABLE_HEIC` is enabled. Metadata tests also verify that `--strip-gps`
removes GPS tags while retaining other EXIF, and that `--strip-all` removes it all.

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
