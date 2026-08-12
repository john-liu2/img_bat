"""Python entry point for the native ``bat_img`` executable."""

from __future__ import annotations

import os
import subprocess
import sys
from importlib.resources import files


def _binary_path() -> str:
    name = "bat_img.exe" if os.name == "nt" else "bat_img"
    binary = files(__package__).joinpath("bin", name)
    return os.fspath(binary)


def main() -> None:
    """Run the packaged C++ executable, preserving its exit status."""
    try:
        completed = subprocess.run([_binary_path(), *sys.argv[1:]], check=False)
    except FileNotFoundError as exc:
        raise SystemExit("bat_img native executable was not installed correctly") from exc
    raise SystemExit(completed.returncode)
