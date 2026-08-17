#!/usr/bin/env python3
"""End-to-end checks for HEIC conversion and Exif stripping."""

from __future__ import annotations

from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile
import tomllib

HERE = Path(__file__).resolve().parent
JPEG_FIXTURE = HERE / "fixtures/source.jpg"
HEIC_FIXTURE = HERE / "fixtures/src.heic"
fixture_lst = [
    ("jpg", JPEG_FIXTURE),
    ("heic", HEIC_FIXTURE),
]


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


def get_exiv2_tags(exiv2_bin: str, file_path: Path) -> list[str]:
    """Extract EXIF/XMP tag keys present in a file using exiv2."""
    if not exiv2_bin or not os.path.exists(exiv2_bin):
        return []
    res = subprocess.run([exiv2_bin, "-pa", str(file_path)],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        return []
    lines = res.stdout.strip().splitlines()
    tags = []
    for line in lines:
        parts = line.split()
        if parts:
            tags.append(parts[0])
    return tags


def test_strip_metadata(img_bat_bin: str, exiv2_bin: str = None):
    """Tests --strip-gps and --strip-all on JPEG and HEIC"""
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)

        for fmt, src in fixture_lst:
            if not src.exists():
                print(f"Skipping {fmt.upper()} test: fixture not found at {src}")
                continue

            # -------------------------------------------------------------
            # Test 1: --strip-gps
            # -------------------------------------------------------------
            target_gps = tmp_path / f"test_gps.{fmt}"
            shutil.copy(src, target_gps)

            print(f"Testing --strip-gps on {target_gps.name}...")
            run(img_bat_bin, "-i", str(target_gps), "--strip-gps", "--overwrite")

            if exiv2_bin:
                tags = get_exiv2_tags(exiv2_bin, target_gps)
                has_gps = any("GPS" in tag for tag in tags)
                assert not has_gps, f"GPS tags still present in {target_gps.name} after --strip-gps"
                print(f"  [PASS] {fmt.upper()} --strip-gps verified (no GPS tags found)")

            # -------------------------------------------------------------
            # Test 2: --strip-all
            # -------------------------------------------------------------
            target_all = tmp_path / f"test_all.{fmt}"
            shutil.copy(src, target_all)

            print(f"Testing --strip-all on {target_all.name}...")
            run(img_bat_bin, "-i", str(target_all), "--strip-all", "--overwrite")

            if exiv2_bin:
                tags = get_exiv2_tags(exiv2_bin, target_all)
                assert len(tags) == 0, f"Metadata tags ({len(tags)}) still present in {target_all.name} after --strip-all"
                print(f"  [PASS] {fmt.upper()} --strip-all verified (0 tags remaining)")

            # Check that output file is non-empty and readable
            assert target_all.stat().st_size > 0, f"{target_all.name} became empty"
            assert target_gps.stat().st_size > 0, f"{target_gps.name} became empty"


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
        # "--quiet",  # not quiet to get more info if fails
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
        str(HEIC_FIXTURE),
        "--format",
        "png",
        "--output",
        str(png_dir),
        # "--quiet",  # not quiet to get more info if fails
    )
    assert (png_dir / "src.png").read_bytes().startswith(
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
        # "--quiet",  # not quiet to get more info if fails
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

    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        if args.mode == "heic":
            test_heic(args.binary, directory)
        elif args.mode == "metadata":
            assert args.exiv2
            test_metadata(args.binary, args.exiv2, directory)
            test_strip_metadata(args.binary, args.exiv2)
        elif args.mode == "status":
            test_status(args.binary, directory)
        elif args.mode == "info":
            test_info(args.binary, directory)
        else:
            test_version(args.binary)


if __name__ == "__main__":
    main()
