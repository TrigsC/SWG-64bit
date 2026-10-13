"""Synthetic archive tests; no game files are needed."""

from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import generate_local_animation_reference as reference
import extract_local_test_assets as test_assets


class LocalAnimationReferenceTests(unittest.TestCase):
    def test_compressed_tre_entry_is_read_without_extraction_to_disk(self):
        name = "appearance/animation/example.ans"
        payload = b"synthetic animation bytes"
        packed_payload = zlib.compress(payload)
        names = name.encode() + b"\0"
        packed_names = zlib.compress(names)
        data_offset = 36
        toc_offset = data_offset + len(packed_payload)
        toc = struct.pack("<6I", 0, len(payload), data_offset, 1,
                          len(packed_payload), 0)
        packed_toc = zlib.compress(toc)
        header = struct.pack("<9I", 0x54524545, 0x30303035, 1, toc_offset,
                             1, len(packed_toc), 1, len(packed_names), len(names))
        with tempfile.TemporaryDirectory() as directory:
            archive_path = Path(directory) / "sample.tre"
            archive_path.write_bytes(header + packed_payload + packed_toc + packed_names)
            archive = reference.TreArchive(archive_path)
            self.assertEqual(archive.extract(name), payload)

    def test_configured_archive_priority_is_descending(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "swgemu_live.cfg").write_text(
                "searchTree_00_01=base.tre\nsearchTree_00_25=patch.tre\n")
            self.assertEqual([path.name for path in reference.archive_paths(root)],
                             ["patch.tre", "base.tre"])

    def test_unreadable_archive_order_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "swgemu_live.cfg").write_text("[SharedFile]\n")
            with self.assertRaisesRegex(ValueError, "no archive searchTree"):
                reference.archive_paths(root)

    def test_output_cannot_escape_ignored_local_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "Output must stay"):
                reference.generate(Path(directory), (), Path(directory) / "public.tsv")

    def test_ui_extraction_only_accepts_expected_relative_paths(self):
        self.assertTrue(test_assets.safe_name("ui/ui_root.ui"))
        self.assertTrue(test_assets.safe_name("string/en/ui.stf"))
        self.assertTrue(test_assets.safe_name("object/mobile/vehicle/example.iff"))
        self.assertFalse(test_assets.safe_name("ui/../private.txt"))
        self.assertFalse(test_assets.safe_name("ui\\private.txt"))
        self.assertFalse(test_assets.safe_name("appearance/mesh/example.mgn"))


if __name__ == "__main__":
    unittest.main()
