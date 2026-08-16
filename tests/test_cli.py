#!/usr/bin/env python3
"""End-to-end checks for HEIC conversion and Exif stripping."""

from __future__ import annotations

from pathlib import Path
import argparse
import os
import sys
import shutil
import subprocess
import tempfile
import tomllib

HERE = Path(__file__).resolve().parent
JPEG_FIXTURE = HERE / "fixtures/source.jpg"
HEIC_FIXTURE = HERE / "fixtures/src.heic"


def pkg_version() -> str:
    with (HERE.parent / "pyproject.toml").open("rb") as f:
        return tomllib.load(f)["project"]["version"]

VERSION = pkg_version()


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
    shutil.copyfile(JPEG_FIXTURE, source)
    return source


def test_heic(binary: str, directory: Path) -> None:
    source = write_jpeg(directory)
    heic_dir = directory / "heic"
    png_dir = directory / "png"

    run(
        binary,
        "--input",
        str(source),
        "--format",
        "heic",
        "--output",
        str(heic_dir),
        "--quiet",
    )
    heic = heic_dir / "source.heic"
    assert heic.exists() and heic.stat().st_size > 0, (
        "HEIC output was not created"
    )

    info_out = run(binary, "--input", str(heic), "--info").stdout
    info_out_lower = info_out.lower()
    assert "ycbcr" in info_out_lower, (
        f"HEIC --info output missing YCbCr colorspace metadata. Got output:\n{info_out}"
    )
    assert "4:2:0" in info_out, (
        f"HEIC --info output missing 4:2:0 chroma metadata. Got output:\n{info_out}"
    )

    run(
        binary,
        "--input",
        str(heic),
        "--format",
        "png",
        "--output",
        str(png_dir),
        # "--quiet",  # not quiet to get more info if "--format png" fails
    )
    assert (png_dir / "source.png").read_bytes().startswith(
        b"\x89PNG\r\n\x1a\n"
    ), "HEIC input did not decode to PNG"

    quality_dir = directory / "quality"
    run(
        binary,
        "--input",
        str(source),
        "--format",
        "heic",
        "--quality",
        "80",
        "--output",
        str(quality_dir),
        "--quiet",
    )
    assert (quality_dir / "source.heic").exists(), (
        "explicit HEIC quality did not produce output"
    )


def test_info(binary: str, directory: Path) -> None:
    source = write_jpeg(directory)
    heic_dir = directory / "heic_info"
    run(
        binary,
        "--input",
        str(source),
        "--format",
        "heic",
        "--output",
        str(heic_dir),
        "--quiet",
    )
    heic = heic_dir / "source.heic"

    result = run(binary, "--input", str(heic), "--info")
    out_lower = result.stdout.lower()
    assert "ycbcr" in out_lower, (
        f"colorspace metadata not reported by --info. Got output:\n{result.stdout}"
    )
    assert "4:2:0" in result.stdout, (
        f"chroma metadata not reported by --info. Got output:\n{result.stdout}"
    )


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

    run(binary, "--input", str(source), "--strip-gps", "--quiet")
    inplace_keys = metadata_keys(exiv2, source)
    assert "Exif.Image.ImageDescription" in inplace_keys and "GPSLatitude" not in inplace_keys, "in-place --strip-gps removed non-GPS metadata"

    # GPS-only edits on HEIC use the in-process BMFF editor: the compressed
    # image item stays untouched, so the file size remains unchanged.
    heic_source = directory / "heic-source.jpg"
    shutil.copyfile(source, heic_source)
    run(exiv2, "-M", "set Exif.GPSInfo.GPSLatitudeRef N", "-M", "set Exif.GPSInfo.GPSLatitude 1/1 2/1 3/1", str(heic_source))
    heic_dir = directory / "heic"
    run(binary, "--input", str(heic_source), "--format", "heic", "--output", str(heic_dir), "--quiet")
    heic = heic_dir / "heic-source.heic"
    before_size = heic.stat().st_size
    assert "GPSLatitude" in metadata_keys(exiv2, heic), "HEIC fixture has no GPS metadata"
    run(binary, "--input", str(heic), "--strip-gps", "--quiet")
    heic_keys = metadata_keys(exiv2, heic)
    assert "Exif.Image.ImageDescription" in heic_keys and "GPSLatitude" not in heic_keys, "HEIC --strip-gps removed non-GPS metadata"
    assert heic.stat().st_size == before_size, "HEIC GPS stripping re-encoded the image"

    batch, batch_out = directory / "batch", directory / "batch-out"
    batch.mkdir()
    for number in range(4):
        shutil.copyfile(source, batch / f"photo-{number}.jpg")
    run(binary, "--input", str(batch), "--output", str(batch_out), "--strip-gps", "--threads", "4", "--quiet")
    for number in range(4):
        keys = metadata_keys(exiv2, batch_out / f"photo-{number}.jpg")
        assert "Exif.Image.ImageDescription" in keys and "GPSLatitude" not in keys, "parallel --strip-gps failed"


def test_status(binary: str, directory: Path) -> None:
    source = write_jpeg(directory)
    result = run(binary, "--input", str(source), "--threads", "1")
    assert "using 1 worker thread" in result.stdout, "worker count was not reported"
    assert "Progress [" in result.stdout and "1/1 (100%)" in result.stdout, "progress was not reported"
    assert "elapsed:" in result.stdout, "elapsed time was not reported"


def test_version(binary: str) -> None:
    for flag in ("-v", "--version"):
        result = run(binary, flag)
        assert result.stdout.strip() == f"img_bat {VERSION}", f"unexpected version output for {flag}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument(
        "--mode",
        choices=("heic", "metadata", "status", "info", "version"),
        required=True,
    )
    parser.add_argument("--exiv2")
    args = parser.parse_args()
    # Set LIBHEIF_PLUGIN_PATH to binary directory if on Windows
    if sys.platform == "win32" and args.binary:
        binary_dir = os.path.dirname(os.path.abspath(args.binary))
        plugin_dir = os.path.join(binary_dir, "libheif")
        if os.path.exists(plugin_dir):
            os.environ["LIBHEIF_PLUGIN_PATH"] = plugin_dir

    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        if args.mode == "heic":
            test_heic(args.binary, directory)
        elif args.mode == "metadata":
            assert args.exiv2
            test_metadata(args.binary, args.exiv2, directory)
        elif args.mode == "status":
            test_status(args.binary, directory)
        elif args.mode == "info":
            test_info(args.binary, directory)
        else:
            test_version(args.binary)


if __name__ == "__main__":
    main()
