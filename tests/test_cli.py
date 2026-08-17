#!/usr/bin/env python3
"""End-to-end checks for HEIC conversion and Exif stripping."""

from __future__ import annotations

from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import tomllib

# PIL & pillow-heif: JPEG, PNG, HEIC support
try:
    from PIL import Image
    import pillow_heif
    pillow_heif.register_heif_opener()
    HAS_HEIC_SUPPORT = True
except ImportError:
    HAS_HEIC_SUPPORT = False


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


def get_image_info(path: Path) -> tuple[int, int, int]:
    """
    Returns (width, height, channels) of an image across platforms.
    Supports JPG, PNG, HEIC, etc. via Pillow + pillow-heif.
    """
    try:
        with Image.open(path) as img:
            w, h = img.size
            channels = 1 if img.mode in ("L", "1", "P") else len(img.getbands())
            return w, h, channels
    except Exception as e:
        print(f"Warning: Failed to inspect {path} using Pillow: {e}", file=sys.stderr)
        return 0, 0, 0

def test_transformation(img_bat_bin: str):
    """Tests all CLI image transformation options."""

    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)

        # 1. Test --rotate DEG (90, 180, 270)
        for fmt, src in fixture_lst:
            orig_w, orig_h, _ = get_image_info(src)
            for deg in (90, 180, 270):
                target = tmp_path / f"test_rotate_{deg}.{fmt}"
                shutil.copy(src, target)
                print(f"Testing {fmt.upper()} --rotate {deg}...")
                run(img_bat_bin, "-i", str(target), "--rotate", str(deg), "--overwrite")
                assert target.stat().st_size > 0, f"Rotated file ({deg}°) is empty"

                if orig_w > 0 and orig_h > 0:
                    w, h, _ = get_image_info(target)
                    if deg in (90, 270):
                        assert (w, h) == (orig_h, orig_w), f"Expected dimensions ({orig_h}x{orig_w}), got ({w}x{h})"
                    else:
                        assert (w, h) == (orig_w, orig_h), f"Expected dimensions ({orig_w}x{orig_h}), got ({w}x{h})"
                print(f"  [PASS] {fmt.upper()} --rotate {deg}")

        # 2. Test --flip-h and --flip-v
        for fmt, src in fixture_lst:
            for flag in ("--flip-h", "--flip-v"):
                target = tmp_path / f"test_{flag.strip('-')}.{fmt}"
                shutil.copy(src, target)
                print(f"Testing {fmt.upper()} {flag}...")
                run(img_bat_bin, "-i", str(target), flag, "--overwrite")
                assert target.stat().st_size > 0, f"Flipped file ({flag}) is empty"
                print(f"  [PASS] {fmt.upper()} {flag}")

        # 3. Test --grayscale
        for fmt, src in fixture_lst:
            orig_w, orig_h, _ = get_image_info(src)
            target_gray = tmp_path / f"test_grayscale.{fmt}"
            shutil.copy(src, target_gray)
            print(f"Testing {fmt.upper()} --grayscale...")
            run(img_bat_bin, "-i", str(target_gray), "--grayscale", "--overwrite")
            assert target_gray.stat().st_size > 0, "Grayscale file is empty"

            if orig_w > 0 and orig_h > 0:
                _, _, channels = get_image_info(target_gray)
                # JPEG might save single channel or 3 identical channels depending on encoder
                assert channels in (1, 3), f"Unexpected channel count: {channels}"
            print(f"  [PASS] {fmt.upper()} --grayscale")

        # 4. Test --brightness N
        for fmt, src in fixture_lst:
            for b in ("15.0", "-15.0"):
                target_b = tmp_path / f"test_brightness_{b}.{fmt}"
                shutil.copy(src, target_b)
                print(f"Testing {fmt.upper()} --brightness {b}...")
                run(img_bat_bin, "-i", str(target_b), "--brightness", b, "--overwrite")
                assert target_b.stat().st_size > 0, "Brightness adjusted file is empty"
                print(f"  [PASS] {fmt.upper()} --brightness {b}")

        # 5. Test --contrast N
        for fmt, src in fixture_lst:
            for c in ("0.8", "1.3"):
                target_c = tmp_path / f"test_contrast_{c}.{fmt}"
                shutil.copy(src, target_c)
                print(f"Testing {fmt.upper()} --contrast {c}...")
                run(img_bat_bin, "-i", str(target_c), "--contrast", c, "--overwrite")
                assert target_c.stat().st_size > 0, "Contrast adjusted file is empty"
                print(f"  [PASS] {fmt.upper()} --contrast {c}")

        # 6. Test --border PX
        for fmt, src in fixture_lst:
            orig_w, orig_h, _ = get_image_info(src)
            border_px = 12
            target_bd = tmp_path / f"test_border.{fmt}"
            shutil.copy(src, target_bd)
            print(f"Testing {fmt.upper()} --border {border_px}...")
            run(img_bat_bin, "-i", str(target_bd), "--border", str(border_px), "--overwrite")
            assert target_bd.stat().st_size > 0, "Border file is empty"

            if orig_w > 0 and orig_h > 0:
                w, h, _ = get_image_info(target_bd)
                expected_w = orig_w + (border_px * 2)
                expected_h = orig_h + (border_px * 2)
                assert (w, h) == (expected_w, expected_h), f"Expected border dimensions ({expected_w}x{expected_h}), got ({w}x{h})"
            print(f"  [PASS] {fmt.upper()} --border {border_px}")

        # 7. Test --resize WxH
        for fmt, src in fixture_lst:
            orig_w, orig_h, _ = get_image_info(src)
            target_sz = tmp_path / f"test_resize.{fmt}"
            shutil.copy(src, target_sz)
            print(f"Testing {fmt.upper()} --resize 120x80...")
            run(img_bat_bin, "-i", str(target_sz), "--resize", "120x80", "--overwrite")
            assert target_sz.stat().st_size > 0, "Resized file is empty"

            if orig_w > 0 and orig_h > 0:
                w, h, _ = get_image_info(target_sz)
                assert (w, h) == (120, 80), f"Expected resized dimensions (120x80), got ({w}x{h})"
            print(f"  [PASS] {fmt.upper()} --resize 120x80")


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
        choices=("heic", "metadata", "status", "info", "version", "transformation"),
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
        elif args.mode == "transformation":
            test_transformation(args.binary)
        else:
            test_version(args.binary)


if __name__ == "__main__":
    main()
