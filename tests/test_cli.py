#!/usr/bin/env python3
"""End-to-end checks for HEIC conversion and Exif stripping."""

from __future__ import annotations

import argparse
import base64
import subprocess
import tempfile
from pathlib import Path


# A tiny valid RGB JPEG; keeping the fixture inline makes the test self-contained.
JPEG = (
    b"/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAP//////////////////////////////////////////////////////////////////////////////////////"
    b"2wBDAf//////////////////////////////////////////////////////////////////////////////////////wAARCAABAAEDASIAAhEBAxEB/8QAFQABAQAAAAAAAAAAAAAAAAAAAAf/"
    b"xAAUEAEAAAAAAAAAAAAAAAAAAAAA/9oADAMBAAIQAxAAAAH/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/9oACAEBAAEFAqf/xAAUEQEAAAAAAAAAAAAAAAAAAAAA/9oACAEDAQE/Aaf/xAAUEQEAAAAAAAAAAAAAAAAAAAAA/9oACAECAQE/Aaf/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/9oACAEBAAY/Aqf/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/9oACAEBAAE/IV//2gAMAwEAAgADAAAAEP/EABQRAQAAAAAAAAAAAAAAAAAAABD/2gAIAQMBAT8QH//EABQRAQAAAAAAAAAAAAAAAAAAABD/2gAIAQIBAT8QH//EABQQAQAAAAAAAAAAAAAAAAAAABD/2gAIAQEAAT8QH//Z"
)


def run(*command: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(command, text=True, capture_output=True)
    if completed.returncode:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed


def write_jpeg(directory: Path) -> Path:
    source = directory / "source.jpg"
    source.write_bytes(base64.b64decode(JPEG))
    return source


def test_heic(binary: str, directory: Path) -> None:
    source = write_jpeg(directory)
    heic_dir, png_dir = directory / "heic", directory / "png"
    run(binary, "--input", str(source), "--format", "heic", "--output", str(heic_dir), "--quiet")
    heic = heic_dir / "source.heic"
    assert heic.exists() and heic.stat().st_size > 0, "HEIC output was not created"
    run(binary, "--input", str(heic), "--format", "png", "--output", str(png_dir), "--quiet")
    assert (png_dir / "source.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), "HEIC input did not decode to PNG"


def metadata_keys(exiv2: str, path: Path) -> str:
    return run(exiv2, "-pa", str(path)).stdout


def test_metadata(binary: str, exiv2: str, directory: Path) -> None:
    source = write_jpeg(directory)
    run(exiv2, "-M", "set Exif.Image.ImageDescription retained", "-M", "set Exif.GPSInfo.GPSLatitudeRef N",
        "-M", "set Exif.GPSInfo.GPSLatitude 1/1 2/1 3/1", str(source))
    gps_dir, all_dir = directory / "gps", directory / "all"
    run(binary, "--input", str(source), "--output", str(gps_dir), "--strip-gps", "--quiet")
    gps_keys = metadata_keys(exiv2, gps_dir / "source.jpg")
    assert "Exif.Image.ImageDescription" in gps_keys and "GPSLatitude" not in gps_keys, "--strip-gps did not preserve non-GPS metadata only"
    run(binary, "--input", str(source), "--output", str(all_dir), "--strip-all", "--quiet")
    assert "Exif." not in metadata_keys(exiv2, all_dir / "source.jpg"), "--strip-all left EXIF metadata"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--mode", choices=("heic", "metadata"), required=True)
    parser.add_argument("--exiv2")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        if args.mode == "heic":
            test_heic(args.binary, directory)
        else:
            assert args.exiv2
            test_metadata(args.binary, args.exiv2, directory)


if __name__ == "__main__":
    main()
