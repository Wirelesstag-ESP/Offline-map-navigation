#!/usr/bin/env python3
"""Validate LVGL 9 RGB565 map tiles produced for map_tiles."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


TILE_SIZE = 256
HEADER = struct.Struct("<BBHHHHH")
EXPECTED_HEADER = (0x19, 0x12, 0, TILE_SIZE, TILE_SIZE, TILE_SIZE * 2, 0)
EXPECTED_FILE_SIZE = HEADER.size + TILE_SIZE * TILE_SIZE * 2


class TileValidationError(ValueError):
    """Raised when a tile does not match the map_tiles data contract."""


def parse_tile_path(root: Path, tile_path: Path) -> tuple[int, int, int]:
    try:
        relative = tile_path.relative_to(root)
    except ValueError as exc:
        raise TileValidationError(f"outside tile root: {tile_path}") from exc

    if len(relative.parts) != 3 or tile_path.suffix.lower() != ".bin":
        raise TileValidationError(f"expected z/x/y.bin path: {relative}")

    try:
        zoom = int(relative.parts[0])
        tile_x = int(relative.parts[1])
        tile_y = int(tile_path.stem)
    except ValueError as exc:
        raise TileValidationError(f"non-numeric tile coordinate: {relative}") from exc

    if zoom < 0 or zoom > 30:
        raise TileValidationError(f"zoom outside 0..30: {relative}")
    coordinate_limit = 1 << zoom
    if not 0 <= tile_x < coordinate_limit or not 0 <= tile_y < coordinate_limit:
        raise TileValidationError(f"coordinate outside zoom {zoom} bounds: {relative}")
    return zoom, tile_x, tile_y


def validate_tile(root: Path, tile_path: Path) -> None:
    parse_tile_path(root, tile_path)
    size = tile_path.stat().st_size
    if size != EXPECTED_FILE_SIZE:
        raise TileValidationError(
            f"wrong size for {tile_path.relative_to(root)}: {size}, expected {EXPECTED_FILE_SIZE}"
        )

    with tile_path.open("rb") as tile_file:
        raw_header = tile_file.read(HEADER.size)
    if len(raw_header) != HEADER.size or HEADER.unpack(raw_header) != EXPECTED_HEADER:
        raise TileValidationError(f"invalid LVGL 9 RGB565 header: {tile_path.relative_to(root)}")


def validate_tree(root: Path) -> tuple[int, list[str]]:
    if not root.is_dir():
        return 0, [f"tile root is not a directory: {root}"]

    tile_paths = sorted(path for path in root.rglob("*") if path.is_file())
    if not tile_paths:
        return 0, [f"no tile files found under: {root}"]

    errors: list[str] = []
    valid_count = 0
    for tile_path in tile_paths:
        try:
            validate_tile(root, tile_path)
            valid_count += 1
        except (OSError, TileValidationError) as exc:
            errors.append(str(exc))
    return valid_count, errors


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate z/x/y.bin tiles for LVGL 9 RGB565 map_tiles input."
    )
    parser.add_argument("tile_root", type=Path, help="Root containing z/x/y.bin files")
    args = parser.parse_args()

    valid_count, errors = validate_tree(args.tile_root)
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    if errors:
        print(f"Validation failed: {valid_count} valid, {len(errors)} invalid", file=sys.stderr)
        return 1

    print(f"Validated {valid_count} tile(s): LVGL 9 RGB565, {TILE_SIZE}x{TILE_SIZE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
