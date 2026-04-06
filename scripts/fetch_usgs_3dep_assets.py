#!/usr/bin/env python3
"""
Fetch and rebuild the optional USGS 3DEP benchmark asset pack.

Requirements:
- Python 3.9+
- ImageMagick 7 available as `magick`
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def load_manifest(manifest_path: Path) -> list[dict]:
    with manifest_path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, list):
        raise RuntimeError(f"expected manifest array in {manifest_path}")
    return data


def ensure_magick() -> str:
    magick = shutil.which("magick")
    if magick is None:
        raise RuntimeError("ImageMagick 7 (`magick`) was not found in PATH")
    return magick


def download(url: str, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url) as response, output_path.open("wb") as handle:
        shutil.copyfileobj(response, handle)


def run_checked(command: list[str]) -> None:
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed: {' '.join(command)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def build_direct_asset(magick: str, entry: dict, output_path: Path) -> None:
    source_url = entry.get("source_url")
    if not source_url:
        raise RuntimeError(f"missing source_url for {entry.get('name', '<unnamed>')}")

    with tempfile.TemporaryDirectory(prefix="usgs_3dep_") as temp_dir:
        temp_tiff = Path(temp_dir) / "source.tiff"
        download(source_url, temp_tiff)
        run_checked(
            [
                magick,
                str(temp_tiff),
                "-auto-level",
                "-colorspace",
                "Gray",
                "-type",
                "Grayscale",
                "-depth",
                "16",
                str(output_path),
            ]
        )


def build_derived_asset(magick: str, entry: dict, source_path: Path, output_path: Path) -> None:
    width = int(entry["width"])
    height = int(entry["height"])
    run_checked(
        [
            magick,
            str(source_path),
            "-filter",
            "Lanczos",
            "-resize",
            f"{width}x{height}!",
            str(output_path),
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        default="assets/benchmarks/terrain/usgs_3dep/manifest.json",
        help="Path to the benchmark manifest, relative to the repository root by default.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Rebuild files even if they already exist.",
    )
    args = parser.parse_args()

    root = repo_root()
    manifest_path = root / args.manifest
    if not manifest_path.exists():
        print(f"manifest not found: {manifest_path}", file=sys.stderr)
        return 2

    try:
        magick = ensure_magick()
        entries = load_manifest(manifest_path)
        entries_by_name = {entry.get("name"): entry for entry in entries}

        for entry in entries:
            output_path = root / entry["png_path"]
            if output_path.exists() and not args.force:
                print(f"skip existing {output_path}")
                continue

            output_path.parent.mkdir(parents=True, exist_ok=True)
            derived_from = entry.get("derived_from")
            if derived_from:
                source_entry = entries_by_name.get(derived_from)
                if source_entry is None:
                    raise RuntimeError(f"missing derived source entry '{derived_from}'")
                source_path = root / source_entry["png_path"]
                if not source_path.exists():
                    build_direct_asset(magick, source_entry, source_path)
                print(f"build derived {output_path.name} from {source_path.name}")
                build_derived_asset(magick, entry, source_path, output_path)
            else:
                print(f"download direct {output_path.name}")
                build_direct_asset(magick, entry, output_path)
    except Exception as exc:  # noqa: BLE001
        print(str(exc), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
