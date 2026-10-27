import struct
import tempfile
import unittest
from pathlib import Path

from tools.validate_map_tiles import (
    EXPECTED_FILE_SIZE,
    EXPECTED_HEADER,
    HEADER,
    TileValidationError,
    parse_tile_path,
    validate_tree,
)


class ValidateMapTilesTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)

    def tearDown(self):
        self.temporary_directory.cleanup()

    def write_tile(self, relative_path: str, header=EXPECTED_HEADER, size=EXPECTED_FILE_SIZE):
        tile_path = self.root / relative_path
        tile_path.parent.mkdir(parents=True, exist_ok=True)
        contents = bytearray(size)
        contents[: HEADER.size] = struct.pack("<BBHHHHH", *header)
        tile_path.write_bytes(contents)
        return tile_path

    def test_valid_tile_tree(self):
        self.write_tile("16/53420/26155.bin")
        self.write_tile("16/53420/26156.bin")

        valid_count, errors = validate_tree(self.root)

        self.assertEqual(valid_count, 2)
        self.assertEqual(errors, [])

    def test_rejects_wrong_header_and_size(self):
        self.write_tile("1/0/0.bin", header=(0, *EXPECTED_HEADER[1:]))
        self.write_tile("1/0/1.bin", size=100)

        valid_count, errors = validate_tree(self.root)

        self.assertEqual(valid_count, 0)
        self.assertEqual(len(errors), 2)

    def test_rejects_invalid_path_and_coordinate(self):
        invalid_path = self.write_tile("16/not-x/26155.bin")
        outside_zoom = self.write_tile("2/4/0.bin")

        with self.assertRaises(TileValidationError):
            parse_tile_path(self.root, invalid_path)
        with self.assertRaises(TileValidationError):
            parse_tile_path(self.root, outside_zoom)

    def test_empty_tree_fails(self):
        valid_count, errors = validate_tree(self.root)

        self.assertEqual(valid_count, 0)
        self.assertEqual(len(errors), 1)


if __name__ == "__main__":
    unittest.main()
