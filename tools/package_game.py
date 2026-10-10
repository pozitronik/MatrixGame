# MatrixGame - licensed under GPLv2 or any later version.
"""Create deterministic archives from a verified Release build and committed source."""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import tarfile
import urllib.request
import zipfile

CONFIG_FILES = ("robots/data.txt", "robots/iface.txt", "standalone.txt")
SYSTEM_DLLS = {"d3d9.dll", "gdi32.dll", "kernel32.dll", "ntdll.dll", "ole32.dll",
               "oleaut32.dll", "shell32.dll", "user32.dll", "winmm.dll", "advapi32.dll",
               "version.dll", "ws2_32.dll", "ucrtbase.dll", "msvcrt.dll"}
EXTERNAL_DLLS = {"d3dx9_43.dll"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(root, *args):
    return subprocess.run(["git", "-C", str(root), *args], capture_output=True,
                          check=True, timeout=60).stdout


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def clean_revision(root):
    if git(root, "status", "--porcelain", "--untracked-files=no").strip():
        raise ValueError("Commit tracked changes before packaging")
    extra = git(root, "ls-files", "--others", "--exclude-standard", "--", "MatrixGame/src",
                "MatrixLib", "ThirdParty", "cmake")
    if extra.strip():
        raise ValueError("Untracked production source prevents source identification")
    return git(root, "rev-parse", "HEAD").decode("ascii").strip()


def cache_values(data):
    result = {}
    for line in data.decode("utf-8").splitlines():
        match = re.match(r"([^#/:=]+):[^=]+=(.*)$", line)
        if match:
            result[match[1]] = match[2]
    return result


def validate_build(cache, binary):
    values = cache_values(cache)
    expected = {"CMAKE_BUILD_TYPE": "Release", "MATRIXGAME_BUILD_DLL": "OFF",
                "MATRIXGAME_CHEATS": "OFF"}
    if any(values.get(key) != value for key, value in expected.items()):
        raise ValueError("Packaging requires a Release standalone EXE with cheats disabled")
    if "winlibs-13.2.0" not in values.get("CMAKE_CXX_COMPILER", "").replace("\\", "/"):
        raise ValueError("Packaging requires the pinned MinGW compiler")
    if len(binary) < 64 or binary[:2] != b"MZ":
        raise ValueError("Invalid executable DOS header")
    offset = struct.unpack_from("<I", binary, 60)[0]
    if offset + 96 > len(binary) or binary[offset:offset + 4] != b"PE\0\0":
        raise ValueError("Invalid executable PE header")
    machine = struct.unpack_from("<H", binary, offset + 4)[0]
    flags = struct.unpack_from("<H", binary, offset + 22)[0]
    magic = struct.unpack_from("<H", binary, offset + 24)[0]
    subsystem = struct.unpack_from("<H", binary, offset + 92)[0]
    if machine != 0x14c or magic != 0x10b or flags & 0x2000 or subsystem != 2:
        raise ValueError("Expected an x86 Windows GUI executable")


def validate_imports(imports):
    names = {name.lower() for name in imports}
    if not {"kernel32.dll", "d3d9.dll", "d3dx9_43.dll"}.issubset(names):
        raise ValueError("Required Windows/DirectX imports are missing")
    for name in names:
        if name not in SYSTEM_DLLS | EXTERNAL_DLLS and not re.fullmatch(r"api-ms-win-crt-[a-z0-9-]+\.dll", name):
            raise ValueError("Undocumented runtime dependency: " + name)
    return sorted(names)


def safe_name(name):
    path = PurePosixPath(name)
    if not name or "\0" in name or "\\" in name or ":" in name or path.is_absolute() or any(p in {"", ".", ".."} for p in name.split("/")):
        raise ValueError("Unsafe archive member: " + name)
    return name


def archive_bytes(files):
    result = io.BytesIO()
    folded = set()
    with zipfile.ZipFile(result, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, data in sorted(files.items()):
            safe_name(name)
            if name.casefold() in folded:
                raise ValueError("Case-insensitive archive collision: " + name)
            folded.add(name.casefold())
            entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    return result.getvalue()


def committed(root, revision, path):
    return git(root, "show", revision + ":" + path)


def runtime_source(root):
    config = json.loads((root / "tools/package-runtime-source.json").read_text(encoding="utf-8"))
    cached = root / ".tools/downloads" / ("mcfgthread-" + config["commit"] + ".tar.gz")
    if not cached.exists():
        request = urllib.request.Request(config["url"], headers={"User-Agent": "MatrixGame-packaging"})
        data = urllib.request.urlopen(request, timeout=60).read()
        if digest(data) != config["sha256"]:
            raise ValueError("Thread-runtime source checksum mismatch")
        cached.parent.mkdir(parents=True, exist_ok=True)
        cached.write_bytes(data)
    data = cached.read_bytes()
    if digest(data) != config["sha256"]:
        raise ValueError("Cached thread-runtime source checksum mismatch")
    return config, data


def package_files(root, build, revision, binary, imports):
    files = {"MatrixGame.exe": binary, "README.md": committed(root, revision, "docs/RELEASE_README.md"),
             "LICENSE": committed(root, revision, "LICENSE"),
             "THIRD_PARTY_NOTICES.md": committed(root, revision, "docs/THIRD_PARTY_NOTICES.md")}
    for name in CONFIG_FILES:
        files["CFG/" + name] = committed(root, revision, "MatrixGame/CFG/" + name)
    for name in ("GCC-runtime-exception.txt", "MinGW-w64-runtime.txt", "MCF-Gthread.txt"):
        files["licenses/" + name] = committed(root, revision, "docs/licenses/" + name)
    files["licenses/libpng.txt"] = (build / "libpng/src/libpng-external/LICENSE").read_bytes()
    files["licenses/zlib.txt"] = (build / "zlib/src/zlib-external/README").read_bytes()
    files["SOURCE.json"] = json_bytes({"revision": revision, "project": "MatrixGame",
        "repository": "https://github.com/pozitronik/MatrixGame", "compiler": "GCC 13.2.0",
        "architecture": "x86", "configuration": "Release", "cheats": False,
        "imports": imports, "external_runtime": sorted(EXTERNAL_DLLS),
        "files": {name: digest(data) for name, data in sorted(files.items())}})
    return files


def source_files(root, revision, runtime_config, runtime_archive):
    files = {}
    raw = git(root, "archive", "--format=tar", revision)
    with tarfile.open(fileobj=io.BytesIO(raw)) as source:
        for item in source:
            if item.isdir():
                continue
            if not item.isfile():
                raise ValueError("Source archive contains a non-regular member: " + item.name)
            name = safe_name(item.name)
            if name.lower().endswith(".pkg") or name.lower() in {"robots.dat", "sounds.txt"} or PurePosixPath(name).name == "YOUR.GITHUB.NAME" or any(p in {".codex", ".claude", ".tools", ".local", ".git"} for p in PurePosixPath(name).parts):
                raise ValueError("Private content in committed source: " + name)
            files[name] = source.extractfile(item).read()
    files["vendor/mcfgthread-source.tar.gz"] = runtime_archive
    files["vendor/mcfgthread-source.json"] = json_bytes(runtime_config)
    return files


def create_package(root, build, output, objdump):
    revision = clean_revision(root)
    cache = (build / "CMakeCache.txt").read_bytes()
    binary = (build / "MatrixGame/MatrixGame.exe").read_bytes()
    receipt = json.loads((build / "packaging-build.json").read_text(encoding="utf-8-sig"))
    if receipt != {"revision": revision, "binary_sha256": digest(binary), "cache_sha256": digest(cache)}:
        raise ValueError("Build receipt does not match the source, executable and configuration")
    validate_build(cache, binary)
    imports = inspect_imports(build / "MatrixGame/MatrixGame.exe", objdump)
    config, runtime = runtime_source(root)
    packages = {"MatrixGame-windows-x86-" + revision[:12] + ".zip": archive_bytes(package_files(root, build, revision, binary, imports)),
                "MatrixGame-source-" + revision[:12] + ".zip": archive_bytes(source_files(root, revision, config, runtime))}
    output.mkdir(parents=True, exist_ok=True)
    for name, data in packages.items():
        destination = output / name
        if destination.exists() and destination.read_bytes() != data:
            raise ValueError("An existing archive has different inputs; use an empty output directory: " + name)
    for name, data in packages.items():
        (output / name).write_bytes(data)
    sums = "".join(digest(data) + "  " + name + "\n" for name, data in sorted(packages.items()))
    (output / ("MatrixGame-" + revision[:12] + ".sha256")).write_bytes(sums.encode("ascii"))
    return sorted(packages)


def inspect_imports(binary, objdump):
    dump = subprocess.run([str(objdump), "-p", str(binary)], capture_output=True,
                          text=True, check=True, timeout=30).stdout
    return validate_imports(re.findall(r"DLL Name:\s+(\S+)", dump))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--objdump", type=Path, required=True)
    args = parser.parse_args()
    try:
        for name in create_package(args.root.resolve(), args.build.resolve(), args.output.resolve(), args.objdump.resolve()):
            print(args.output / name)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
