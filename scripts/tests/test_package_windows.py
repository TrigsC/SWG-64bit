import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("package_windows", Path(__file__).resolve().parents[1] / "package_windows.py")
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)


class WindowsPackageTests(unittest.TestCase):
    def test_only_expected_x64_outputs_and_notices_are_packaged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            header = bytearray(64)
            header[:2] = b"MZ"
            struct.pack_into("<I", header, 60, 64)
            for relative in ("Release/SWGLauncher.exe", "Release/windows-client-smoke.exe", "swg/tools/dummyclient/Release/dummyclient.exe"):
                path = build / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(header + b"PE\0\0\x64\x86")
            for name in bundle.SHADERS:
                path = build / "swg/libs/renderer/shaders" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"\x03\x02\x23\x07")
            notice = root / "notice.txt"
            notice.write_text("Synthetic test notice")
            # These synthetic extra files must never be swept into a bundle.
            (build / "not-an-output.tre").write_text("synthetic")
            with patch.object(bundle, "BUILD", build), patch.object(bundle, "NOTICES", {"notice.txt": notice}):
                output = root / "output"
                bundle.package(output, "synthetic-revision")
                manifest = json.loads((output / "build.json").read_text())
                self.assertEqual(manifest["architecture"], "x64")
                self.assertEqual(len(manifest["sha256"]), 14)
                self.assertFalse((output / "not-an-output.tre").exists())
                self.assertIn("shaders/Wireframe_VSMain.spv", manifest["sha256"])
                with self.assertRaises(FileExistsError):
                    bundle.package(output, "another-revision")

    def test_wrong_architecture_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "synthetic.exe"
            header = bytearray(64)
            header[:2] = b"MZ"
            struct.pack_into("<I", header, 60, 64)
            path.write_bytes(header + b"PE\0\0\x64\xaa")
            with self.assertRaises(ValueError):
                bundle.x64_executable(path)
