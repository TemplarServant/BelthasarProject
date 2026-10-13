#!/usr/bin/env python3
"""ROM-free regression checks for the build driver and macOS packager.

Run with python3 -m unittest discover -s tests. Build integration checks require
CMake, Ninja and a C compiler on macOS/Linux; packaging checks use fake data.
"""

import contextlib
import errno
import importlib.util
import io
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


def load_tool(name):
    path = Path(__file__).resolve().parent.parent / "tools" / (name + ".py")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


build = load_tool("build")
package = load_tool("package_macos")
BUNDLE_ID = "io.github.TemplarServant.BelthasarProject"


class CMakeReplyChecks(unittest.TestCase):
    def test_missing_or_invalid_source_metadata_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary, \
                mock.patch.object(build, "BUILD", temporary):
            reply = Path(temporary) / ".cmake/api/v1/reply"
            for metadata in (None, "not JSON", "{}"):
                with self.subTest(metadata=metadata):
                    if metadata is not None:
                        reply.mkdir(parents=True, exist_ok=True)
                        (reply / "index-2000-01-01.json").write_text(metadata)
                    with contextlib.redirect_stdout(io.StringIO()) as output, \
                            self.assertRaises(SystemExit) as raised:
                        build.configured_source_lists()
                    self.assertEqual(raised.exception.code, 1)
                    self.assertIn("BUILD FAILED", output.getvalue())


class PackagingChecks(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "build"
        self.build.mkdir()
        (self.build / "ct_field.exe").write_bytes(b"synthetic engine")
        patch = mock.patch.multiple(package, ROOT=self.root, BUILD=self.build)
        patch.start()
        self.addCleanup(patch.stop)

    def make_previous_app(self, path):
        contents = path / "Contents"
        contents.mkdir(parents=True)
        (contents / "Info.plist").write_bytes(plistlib.dumps({"CFBundleIdentifier": BUNDLE_ID}))
        marker = contents / "personal-state"
        marker.write_bytes(b"synthetic state to preserve")
        return marker

    def package_to(self, output):
        with mock.patch.object(sys, "argv", ["package_macos.py", "--output", str(output)]), \
                mock.patch.object(package.platform, "system", return_value="Darwin"), \
                mock.patch.object(package.shutil, "copy2", side_effect=OSError("synthetic staging failure")), \
                contextlib.redirect_stderr(io.StringIO()):
            return package.main()

    def test_staging_destinations_are_rejected_before_deletion(self):
        for relative in ("Chrono Trigger.app", "Previous.app", "nested/Previous.app"):
            with self.subTest(output=relative):
                output = self.build / "macos-staging" / relative
                marker = self.make_previous_app(output)
                with self.assertRaises(SystemExit) as raised:
                    self.package_to(output)
                self.assertEqual(raised.exception.code, 2)
                self.assertEqual(marker.read_bytes(), b"synthetic state to preserve")

    def test_symlink_into_staging_is_rejected(self):
        output = self.build / "macos-staging" / "Previous.app"
        marker = self.make_previous_app(output)
        alias = self.root / "Alias.app"
        try:
            alias.symlink_to(output, target_is_directory=True)
        except OSError as error:
            self.skipTest("symlinks unavailable: " + str(error))
        with self.assertRaises(SystemExit) as raised:
            self.package_to(alias)
        self.assertEqual(raised.exception.code, 2)
        self.assertEqual(marker.read_bytes(), b"synthetic state to preserve")

    def test_external_app_survives_failed_staging(self):
        marker = self.make_previous_app(self.root / "saved" / "Previous.app")
        with self.assertRaisesRegex(OSError, "synthetic staging failure"):
            self.package_to(marker.parent.parent)
        self.assertEqual(marker.read_bytes(), b"synthetic state to preserve")

    def complete_package_to(self, output):
        files = {
            "platform/macos/Info.plist": plistlib.dumps({
                "CFBundleIdentifier": BUNDLE_ID,
                "CFBundleShortVersionString": "0.0.0", "CFBundleVersion": "1"}),
            "LICENSE": b"synthetic license",
            "snes/LICENSE.LakeSnes.txt": b"synthetic license",
        }
        for name, content in files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        with mock.patch.object(sys, "argv", ["package_macos.py", "--output", str(output)]), \
             mock.patch.object(package.platform, "system", return_value="Darwin"), \
             mock.patch.object(package, "dependencies", return_value=[]), \
             mock.patch.object(package, "rpaths", return_value=[]), \
             mock.patch.object(package, "source_revision", return_value=(None, None)), \
             mock.patch.object(package, "run", return_value=""), \
             contextlib.redirect_stdout(io.StringIO()):
            return package.main()

    def test_destination_copy_failure_preserves_previous_app(self):
        output = self.root / "saved" / "Previous.app"
        marker = self.make_previous_app(output)
        with mock.patch("os.rename", side_effect=OSError(errno.EXDEV, "another volume")), \
             mock.patch.object(package.shutil, "copytree", side_effect=OSError("destination full")), \
             self.assertRaisesRegex(OSError, "destination full"):
            self.complete_package_to(output)
        self.assertEqual(marker.read_bytes(), b"synthetic state to preserve")

    def test_successful_package_replaces_previous_app(self):
        output = self.root / "saved" / "Previous.app"
        marker = self.make_previous_app(output)
        self.assertEqual(self.complete_package_to(output), 0)
        self.assertFalse(marker.exists())
        self.assertEqual((output / "Contents/MacOS/ChronoTrigger").read_bytes(), b"synthetic engine")

    def test_destination_rename_failure_restores_previous_app(self):
        output = (self.root / "saved" / "Previous.app").resolve()
        marker = self.make_previous_app(output)
        real_rename = Path.rename
        failed = False

        def rename(source, destination):
            nonlocal failed
            if Path(destination) == output and not failed:
                failed = True
                raise OSError("destination rename failed")
            return real_rename(source, destination)

        with mock.patch.object(Path, "rename", new=rename), \
             self.assertRaisesRegex(OSError, "destination rename failed"):
            self.complete_package_to(output)
        self.assertEqual(marker.read_bytes(), b"synthetic state to preserve")

    def test_failed_restore_keeps_backup_for_recovery(self):
        output = (self.root / "saved" / "Previous.app").resolve()
        self.make_previous_app(output)
        real_rename = Path.rename

        def rename(source, destination):
            if Path(destination) == output:
                raise OSError("destination unavailable")
            return real_rename(source, destination)

        with mock.patch.object(Path, "rename", new=rename), \
             self.assertRaisesRegex(RuntimeError, "Previous app kept at"):
            self.complete_package_to(output)
        backups = list(output.parent.glob(".chrono-trigger-*/previous.app/Contents/personal-state"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), b"synthetic state to preserve")


@unittest.skipUnless(os.name != "nt" and shutil.which("cmake") and shutil.which("ninja"),
                     "build integration requires macOS/Linux with CMake and Ninja on PATH")
class BuildFreshnessChecks(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / "build"
        patch = mock.patch.multiple(build, ROOT=str(self.root), BUILD=str(self.output),
                                    STAMP_PATH=str(self.output / ".ctbuildstamp.json"))
        patch.start()
        self.addCleanup(patch.stop)
        (self.root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.15)
project(host_tools_fixture C)
add_executable(ct main.c)
set_target_properties(ct PROPERTIES SUFFIX ".exe")
target_sources(ct PRIVATE helper.c)
option(EXTRA_SOURCE "" ON)
if(EXTRA_SOURCE)
  target_sources(ct PRIVATE enabled.c)
else()
  target_sources(ct PRIVATE disabled.c)
endif()
target_sources(ct PRIVATE "$<$<PLATFORM_ID:Darwin>:mac.c>"
                          "$<$<NOT:$<PLATFORM_ID:Darwin>>:other.c>")
add_executable(ct_viewer viewer.c)
set_target_properties(ct_viewer PROPERTIES SUFFIX ".exe")
''')
        (self.root / "main.c").write_text("int value(void); int main(void) { return value(); }\n")
        (self.root / "helper.c").write_text("int value(void) { return 3; }\n")
        for name in ("enabled", "disabled", "mac", "other"):
            (self.root / (name + ".c")).write_text("int " + name + "(void) { return 0; }\n")
        (self.root / "viewer.c").write_text("int main(void) { return 0; }\n")

    def run_build(self, target="ct"):
        output = io.StringIO()
        # Keep the fixture's configuration independent of the caller's settings.
        with mock.patch.dict(os.environ, {"CT_BUILD_TYPE": "RelWithDebInfo"}), \
                contextlib.redirect_stdout(output):
            result = build.main([] if target is None else [target])
        self.assertEqual(result, 0, output.getvalue())
        return output.getvalue()

    def stamp(self):
        return json.loads((self.output / ".ctbuildstamp.json").read_text())["targets"]

    def test_configured_sources_and_requested_target_are_preserved(self):
        self.run_build()
        sources = self.stamp()["ct"]["sources"]
        active = "mac.c" if sys.platform == "darwin" else "other.c"
        inactive = "other.c" if sys.platform == "darwin" else "mac.c"
        self.assertEqual(set(sources), {"main.c", "helper.c", "enabled.c", active})
        self.assertNotIn(inactive, sources)
        self.assertNotIn("ct_viewer", self.stamp())
        self.assertFalse((self.output / "ct_viewer.exe").exists())

    def test_configured_target_does_not_need_pkg_config(self):
        real_which = shutil.which
        with mock.patch.object(build.shutil, "which", side_effect=lambda name:
                               None if name == "pkg-config" else real_which(name)):
            self.run_build("ct_viewer")
        self.assertIn("ct_viewer", self.stamp())
        self.assertEqual(subprocess.run([str(self.output / "ct_viewer.exe")]).returncode, 0)

    def test_all_configured_targets_are_verified_without_pkg_config(self):
        real_which = shutil.which
        with mock.patch.object(build.shutil, "which", side_effect=lambda name:
                               None if name == "pkg-config" else real_which(name)):
            self.run_build(None)
        self.assertEqual(set(self.stamp()), {"ct", "ct_viewer"})

    def test_unconfigured_target_is_rejected_even_with_old_executable(self):
        self.run_build("ct_viewer")
        cmake = self.root / "CMakeLists.txt"
        cmake.write_text(cmake.read_text().replace(
            'add_executable(ct_viewer viewer.c)\nset_target_properties(ct_viewer PROPERTIES SUFFIX ".exe")\n', ''))
        self.assertTrue((self.output / "ct_viewer.exe").is_file())
        with self.assertRaises(SystemExit) as raised:
            self.run_build("ct_viewer")
        self.assertEqual(raised.exception.code, 2)

    def test_stale_mtime_edit_in_target_sources_rebuilds_executable(self):
        self.run_build()
        executable = self.output / "ct.exe"
        self.assertEqual(subprocess.run([str(executable)]).returncode, 3)
        helper = self.root / "helper.c"
        old = helper.stat()
        helper.write_text("int value(void) { return 4; }\n")
        os.utime(helper, ns=(old.st_atime_ns, old.st_mtime_ns))
        output = self.run_build()
        self.assertIn("CLOCK-SKEW TRAP", output)
        self.assertEqual(subprocess.run([str(executable)]).returncode, 4)

    def test_older_stamp_accepts_newly_tracked_sources(self):
        self.run_build()
        stamp_path = self.output / ".ctbuildstamp.json"
        stamp = json.loads(stamp_path.read_text())
        sources = stamp["targets"]["ct"]["sources"]
        stamp["targets"]["ct"]["sources"] = {"main.c": sources["main.c"]}
        stamp_path.write_text(json.dumps(stamp))
        self.run_build()
        self.assertIn("helper.c", self.stamp()["ct"]["sources"])

    def test_older_stamp_rebuilds_newly_tracked_stale_source(self):
        self.run_build()
        stamp_path = self.output / ".ctbuildstamp.json"
        stamp = json.loads(stamp_path.read_text())
        del stamp["targets"]["ct"]["sources"]["helper.c"]
        stamp_path.write_text(json.dumps(stamp))
        helper = self.root / "helper.c"
        old = helper.stat()
        helper.write_text("int value(void) { return 4; }\n")
        os.utime(helper, ns=(old.st_atime_ns, old.st_mtime_ns))
        self.run_build()
        self.assertEqual(subprocess.run([str(self.output / "ct.exe")]).returncode, 4)


if __name__ == "__main__":
    unittest.main()
