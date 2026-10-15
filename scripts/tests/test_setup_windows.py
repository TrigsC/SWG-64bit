"""Asset-free prerequisite and command-routing checks for Windows setup."""
import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("setup_windows", Path(__file__).resolve().parents[1] / "setup_windows.py")
setup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(setup)


def ready():
    return dict(system="Windows", machine="AMD64", free_gib=10,
                git="git", cmake="cmake", ctest="ctest", vs2022="VS2022",
                vulkan_sdk="sdk", vulkan_headers=True, vulkan_library=True, dxc=True)


class WindowsSetupTests(unittest.TestCase):
    def test_native_architecture_wins_over_emulated_python(self):
        def probe(process, process_machine, native_machine):
            native_machine._obj.value = 0xAA64
            return True
        with patch.object(setup.platform, "system", return_value="Windows"), \
             patch.object(setup.platform, "machine", return_value="AMD64"), \
             patch.object(setup.ctypes, "WinDLL", create=True) as library:
            library.return_value.IsWow64Process2.side_effect = probe
            self.assertEqual(setup.host_machine(), "ARM64")

    def test_core_does_not_require_vulkan(self):
        info = ready()
        for key in ("vulkan_headers", "vulkan_library", "dxc"):
            info[key] = False
        self.assertEqual(setup.missing(info, core=True), [])
        self.assertEqual(len(setup.missing(info)), 3)

    def test_arm64_development_is_allowed_with_warning(self):
        info = ready()
        info["machine"] = "ARM64"
        self.assertEqual(setup.missing(info), [])
        with patch.object(setup, "inventory", return_value=info), patch("builtins.print") as output:
            with patch("sys.argv", ["setup_windows.py", "--check"]):
                setup.main()
        self.assertTrue(any("emulation" in str(call) for call in output.call_args_list))

    def test_missing_compiler_is_actionable(self):
        info = ready()
        info["vs2022"] = ""
        self.assertIn("Missing vs2022", setup.missing(info))

    @patch.object(setup, "inventory", side_effect=ready)
    @patch.object(setup.subprocess, "run")
    def test_check_never_builds(self, run, inventory):
        with patch("sys.argv", ["setup_windows.py", "--check"]):
            setup.main()
        run.assert_not_called()

    @patch.object(setup, "inventory", side_effect=ready)
    @patch.object(setup.subprocess, "run")
    def test_core_build_then_tests_without_graphics(self, run, inventory):
        with patch("sys.argv", ["setup_windows.py", "--core"]):
            setup.main()
        self.assertEqual(run.call_count, 3)
        self.assertIn("-DSWG_BUILD_RENDERER=OFF", run.call_args_list[0].args[0])
        self.assertEqual(run.call_args_list[-1].args[0][0], "ctest")
        self.assertTrue(all(call.kwargs["check"] for call in run.call_args_list))

    @patch.object(setup, "inventory", side_effect=ready)
    @patch.object(setup.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "cmake"))
    def test_failed_configure_does_not_continue(self, run, inventory):
        with patch("sys.argv", ["setup_windows.py", "--core"]):
            with self.assertRaises(subprocess.CalledProcessError):
                setup.main()
        self.assertEqual(run.call_count, 1)

    @patch.object(setup, "inventory", side_effect=ready)
    @patch.object(setup.subprocess, "run")
    def test_render_probe_is_explicit_and_bounded(self, run, inventory):
        with patch("sys.argv", ["setup_windows.py", "--render-smoke"]):
            setup.main()
        self.assertEqual(run.call_count, 4)
        self.assertEqual(run.call_args.args[0][-1], "--render")
        self.assertEqual(run.call_args.kwargs["timeout"], 30)

    @patch.object(setup, "inventory", side_effect=ready)
    @patch.object(setup.subprocess, "run")
    def test_release_configuration_is_used_for_build_and_tests(self, run, inventory):
        with patch("sys.argv", ["setup_windows.py", "--configuration", "Release"]):
            setup.main()
        self.assertIn("Release", run.call_args_list[1].args[0])
        self.assertEqual(run.call_args_list[2].args[0][-2:], ("-C", "Release"))


if __name__ == "__main__":
    unittest.main()
