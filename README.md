# EXR Cleanup Tool

[![CI](https://github.com/ginzburg-dev/EXR-Cleanup-Tool/actions/workflows/ci.yml/badge.svg)](https://github.com/ginzburg-dev/EXR-Cleanup-Tool/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Removes RenderMan variance channels from OpenEXR files.

## How it works

`exr-cleanup` accepts an EXR file or a directory. Directories are scanned
recursively by default.

The following channel layers are removed:

- `diffuse_mse`
- `specular_mse`
- `normal_var`

All other channels and header attributes are copied to the output file. The
source is replaced after the output file has been written.

Only single-part scanline EXR files are supported.

## Requirements

- A C++17 compiler
- CMake 3.20 or newer
- OpenEXR 3.x

### Install OpenEXR

macOS with Homebrew:

```sh
brew install cmake openexr
```

Ubuntu or Debian:

```sh
sudo apt-get install cmake g++ libopenexr-dev
```

Windows with vcpkg:

```powershell
vcpkg install openexr:x64-windows
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

On Windows, add the vcpkg toolchain when configuring:

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

To install the executable:

```sh
cmake --install build
```

## Usage

Inspect a directory recursively without changing anything:

```sh
exr-cleanup --dry-run /path/to/renders
```

Clean one file and keep the original:

```sh
exr-cleanup --keep-backup /path/to/render_variance.exr
```

Clean only the EXR files directly inside a directory:

```sh
exr-cleanup --no-recursive /path/to/renders
```

Run `exr-cleanup --help` for all options. The process exits with `0` when all
files were handled, `1` when one or more files failed, and `2` for invalid
command-line usage.
