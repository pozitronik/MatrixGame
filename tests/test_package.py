# MatrixGame - licensed under GPLv2 or any later version.
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

PROJECT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("package_game", PROJECT / "tools/package_game.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


def executable():
    data = bytearray(256)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<H", data, 68, 0x14c)
    struct.pack_into("<H", data, 86, 0x102)
    struct.pack_into("<H", data, 88, 0x10b)
    struct.pack_into("<H", data, 156, 2)
    return bytes(data)


class PackageTests(unittest.TestCase):
    def setUp(self):
        base = PROJECT / "build/package-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build/mingw-release-exe"
        self.build.mkdir(parents=True)
        self.command("init", "-q")
        self.command("config", "user.name", "Fixture")
        self.command("config", "user.email", "fixture@example.invalid")
        self.command("config", "core.autocrlf", "false")
        self.write(".gitignore", b"build/\n.tools/\n.codex/\n*.pkg\nrobots.dat\nsounds.txt\n")
        for name in ["LICENSE", "docs/RELEASE_README.md", "docs/THIRD_PARTY_NOTICES.md",
                     "docs/licenses/GCC-runtime-exception.txt", "docs/licenses/MinGW-w64-runtime.txt",
                     "docs/licenses/MCF-Gthread.txt"]:
            self.write(name, ("synthetic " + name).encode())
        for name in package.CONFIG_FILES:
            self.write("MatrixGame/CFG/" + name, b"synthetic committed defaults")
        runtime = b"synthetic cached source archive"
        self.config = {"commit": "a" * 40, "sha256": package.digest(runtime), "version": "fixture", "url": "https://example.invalid"}
        self.write("tools/package-runtime-source.json", package.json_bytes(self.config))
        self.write(".tools/downloads/mcfgthread-" + self.config["commit"] + ".tar.gz", runtime)
        self.command("add", ".")
        self.command("commit", "-qm", "Synthetic fixture")
        self.revision = self.command("rev-parse", "HEAD").decode().strip()
        self.binary = executable()
        self.cache = b"CMAKE_BUILD_TYPE:STRING=Release\nMATRIXGAME_BUILD_DLL:BOOL=OFF\nMATRIXGAME_CHEATS:BOOL=OFF\nCMAKE_CXX_COMPILER:FILEPATH=C:/fixture/.tools/winlibs-13.2.0/mingw32/bin/g++.exe\n"
        self.write("build/mingw-release-exe/CMakeCache.txt", self.cache)
        self.write("build/mingw-release-exe/MatrixGame/MatrixGame.exe", self.binary)
        self.write("build/mingw-release-exe/libpng/src/libpng-external/LICENSE", b"synthetic png license")
        self.write("build/mingw-release-exe/zlib/src/zlib-external/README", b"synthetic zlib license")
        self.write("build/mingw-release-exe/packaging-build.json", package.json_bytes({"revision": self.revision,
            "binary_sha256": package.digest(self.binary), "cache_sha256": package.digest(self.cache)}))

    def command(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args], capture_output=True, check=True).stdout

    def write(self, relative, data):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def create(self, output="build/packages"):
        with patch.object(package, "inspect_imports", return_value=["d3d9.dll", "d3dx9_43.dll", "kernel32.dll"]):
            return package.create_package(self.root, self.build, self.root / output, Path("unused-objdump"))

    def test_explicit_files_exclude_contaminated_output(self):
        for relative in ["DATA/robots.pkg", "DATA/sound.pkg", "CFG/robots.dat", "CFG/sounds.txt", "test.log", "probe.exe", "stray.dll"]:
            self.write("build/mingw-release-exe/MatrixGame/" + relative, b"private poison")
        self.write("build/mingw-release-exe/MatrixGame/CFG/robots/data.txt", b"private override")
        names = self.create()
        binary = next(name for name in names if "windows" in name)
        with zipfile.ZipFile(self.root / "build/packages" / binary) as archive:
            self.assertNotIn("DATA/robots.pkg", archive.namelist())
            self.assertFalse(any(name.endswith((".pkg", ".dll", ".dat", ".log")) for name in archive.namelist()))
            self.assertEqual(archive.read("CFG/robots/data.txt"), b"synthetic committed defaults")
            self.assertFalse(any(b"private" in archive.read(name) for name in archive.namelist()))
            info = json.loads(archive.read("SOURCE.json"))
            self.assertEqual(info["revision"], self.revision)
            for name, expected in info["files"].items():
                self.assertEqual(package.digest(archive.read(name)), expected)

    def test_archive_bytes_and_paths_are_reproducible(self):
        names = self.create()
        self.create("build/another-output")
        for name in names:
            self.assertEqual((self.root / "build/packages" / name).read_bytes(),
                             (self.root / "build/another-output" / name).read_bytes())
        self.assertEqual(package.archive_bytes({"b": b"2", "a": b"1"}), package.archive_bytes({"a": b"1", "b": b"2"}))

    def test_source_pair_contains_committed_code_and_runtime(self):
        names = self.create()
        source = next(name for name in names if "source" in name)
        with zipfile.ZipFile(self.root / "build/packages" / source) as archive:
            self.assertIn("tools/package-runtime-source.json", archive.namelist())
            self.assertIn("vendor/mcfgthread-source.tar.gz", archive.namelist())
            self.assertEqual(json.loads(archive.read("vendor/mcfgthread-source.json")), self.config)
            self.assertFalse(any(name.startswith(("build/", ".tools/", ".codex/")) for name in archive.namelist()))

    def test_stale_build_receipt_rejected(self):
        self.write("build/mingw-release-exe/MatrixGame/MatrixGame.exe", self.binary + b"changed")
        with self.assertRaisesRegex(ValueError, "receipt"):
            self.create()

    def test_dirty_and_untracked_production_source_rejected(self):
        self.write("MatrixGame/CFG/standalone.txt", b"edited")
        with self.assertRaisesRegex(ValueError, "Commit tracked"):
            self.create()
        self.command("checkout", "--", "MatrixGame/CFG/standalone.txt")
        self.write("MatrixGame/src/untracked.cpp", b"unexpected source")
        with self.assertRaisesRegex(ValueError, "Untracked production"):
            self.create()

    def test_wrong_configuration_and_architecture_rejected(self):
        for needle, replacement in [(b"=Release", b"=Debug"), (b"BUILD_DLL:BOOL=OFF", b"BUILD_DLL:BOOL=ON"),
                                    (b"CHEATS:BOOL=OFF", b"CHEATS:BOOL=ON"), (b"winlibs-13.2.0", b"unknown-tools")]:
            with self.assertRaises(ValueError):
                package.validate_build(self.cache.replace(needle, replacement), self.binary)
        wrong = bytearray(self.binary)
        struct.pack_into("<H", wrong, 68, 0x8664)
        with self.assertRaisesRegex(ValueError, "x86"):
            package.validate_build(self.cache, wrong)
        for offset, value in [(86, 0x2102), (156, 3)]:
            wrong = bytearray(self.binary)
            struct.pack_into("<H", wrong, offset, value)
            with self.assertRaisesRegex(ValueError, "GUI executable"):
                package.validate_build(self.cache, wrong)

    def test_unknown_or_incomplete_imports_rejected(self):
        with self.assertRaises(ValueError):
            package.validate_imports([])
        with self.assertRaisesRegex(ValueError, "Undocumented"):
            package.validate_imports(["kernel32.dll", "d3d9.dll", "d3dx9_43.dll", "libmcfgthread-1.dll"])

    def test_unsafe_archive_names_and_collisions_rejected(self):
        for name in ["../secret", "C:/secret", "a\\secret", "/secret", "a//secret", ""]:
            with self.assertRaises(ValueError):
                package.archive_bytes({name: b"x"})
        with self.assertRaisesRegex(ValueError, "collision"):
            package.archive_bytes({"A": b"1", "a": b"2"})

    def test_existing_different_archive_is_preserved(self):
        names = self.create()
        path = self.root / "build/packages" / names[0]
        path.write_bytes(b"existing unrelated result")
        with self.assertRaisesRegex(ValueError, "different inputs"):
            self.create()
        self.assertEqual(path.read_bytes(), b"existing unrelated result")


if __name__ == "__main__":
    unittest.main()
