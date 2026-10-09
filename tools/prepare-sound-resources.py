#!/usr/bin/env python3
"""Prepare private standalone sound resources from an owned game installation.

DAT format reference: https://github.com/denballakh/ranger-tools/blob/master/rangers/dat.py
No game files are modified, and no audio payload is extracted.
"""
import argparse
import re
import shutil
from pathlib import Path
import hashlib
import struct
import zlib

MODULUS = 2147483647
FORMAT_KEYS = {"HDMain": -1310144887, "ReloadMain": 1050086386, "HDCache": -359710921, "ReloadCache": 1929242201}


def decode_main(raw):
    if len(raw) < 20:
        raise ValueError("DAT input is too short")
    sig1, sig2 = struct.unpack_from("<II", raw)
    body = raw[8:]
    expected1 = len(body) ^ 0xC83FCBF3 ^ 0x7DB6C99D
    expected2 = zlib.crc32(struct.pack("<I", zlib.crc32(body) ^ 0x7DB6C99D) + body) ^ 0xC83FCBF3
    if (sig1, sig2) == (expected1, expected2):
        raw = body
    checksum, encrypted_seed = struct.unpack_from("<Ii", raw)
    for name, key in FORMAT_KEYS.items():
        state = encrypted_seed ^ key
        clear = bytearray()
        for byte in raw[8:]:
            high, low = divmod(state, 127773)
            state = low * 16807 - high * 2836
            if state < 1:
                state += MODULUS
            clear.append(byte ^ ((state - 1) & 255))
        if clear[:4] != b"ZL01" or zlib.crc32(clear) != checksum:
            continue
        expected_size = struct.unpack_from("<I", clear, 4)[0]
        if expected_size > 16 * 1024 * 1024:
            raise ValueError("Decoded configuration exceeds the inspection limit")
        inflater = zlib.decompressobj()
        result = inflater.decompress(clear[8:], expected_size + 1)
        if len(result) != expected_size or not inflater.eof or inflater.unused_data:
            raise ValueError("Invalid DAT compressed content")
        return name, b"\x02\x00\x00" + result
    raise ValueError("Unsupported DAT format or checksum")


class TreeReader:
    def __init__(self, data, format_name="HDMain"):
        self.data = data
        self.has_sorting_flags = format_name.endswith("Main")
        self.offset = 0
        self.items = 0

    def take(self, size):
        if size < 0 or size > len(self.data) - self.offset:
            raise ValueError("Truncated DAT tree")
        start = self.offset
        self.offset += size
        return self.data[start:self.offset]

    def text(self):
        start = self.offset
        while self.take(2) != b"\x00\x00":
            pass
        return self.data[start:self.offset - 2].decode("utf-16le")

    def node(self, depth=0):
        self.items += 1
        if depth > 64 or self.items > 100000:
            raise ValueError("DAT tree exceeds the inspection limit")
        kind = self.take(1)[0]
        name = self.text()
        if kind == 1:
            return {"name": name, "value": self.text()}
        if kind != 2:
            raise ValueError("Invalid DAT tree node")
        ordered = self.take(1)[0] if self.has_sorting_flags else 0
        if ordered not in (0, 1):
            raise ValueError("Invalid DAT sorting flag")
        count = struct.unpack("<I", self.take(4))[0]
        children = []
        for _ in range(count):
            if ordered:
                self.take(8)
            children.append(self.node(depth + 1))
        return {"name": name, "sorted": bool(ordered), "children": children}


def copy_package(source, destination):
    if not source.is_file():
        raise ValueError(f"Missing resource package: {source.name}")
    source_hash = hashlib.sha256(source.read_bytes()).digest()
    if destination.exists():
        if hashlib.sha256(destination.read_bytes()).digest() != source_hash:
            raise ValueError(f"Existing {destination.name} differs; preserve it and choose the intended resource set")
        return
    shutil.copyfile(source, destination)
    if hashlib.sha256(destination.read_bytes()).digest() != source_hash:
        raise ValueError(f"Resource copy failed verification: {destination.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-directory", type=Path, required=True)
    parser.add_argument("--voices", choices=["Rus", "Eng", "None"], default="Rus")
    parser.add_argument("--destination", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    source = args.game_directory / "CFG" / "CacheData.dat"
    raw = source.read_bytes()
    fmt, decoded = decode_main(raw)
    reader = TreeReader(decoded, fmt)
    tree = reader.node()
    if reader.offset != len(decoded):
        raise ValueError("Configuration has trailing data")
    blocks = [node for node in tree["children"] if node["name"].lower() == "sound"]
    if len(blocks) != 1:
        raise ValueError("Expected one Sound definition block")
    mappings = []
    for node in blocks[0]["children"]:
        if "value" not in node:
            raise ValueError("Nested sound definitions need an explicit mapping")
        name, path = node["name"], node["value"]
        if not re.fullmatch(r"[A-Za-z0-9_]+", name) or any(c in path for c in "\r\n{}"):
            raise ValueError("Unsupported sound definition syntax")
        mappings.append(f"    {name}={path}")
    text = "Sound {\n" + "\n".join(mappings) + "\n}\n"
    encoded = text.encode("utf-16")
    target = args.destination.resolve()
    target.mkdir(parents=True, exist_ok=True)
    mapping_file = target / "sounds.txt"
    if mapping_file.exists() and mapping_file.read_bytes() != encoded:
        raise ValueError("Existing sounds.txt differs; preserve it before preparing another resource set")
    copy_package(args.game_directory / "DATA" / "Sound.pkg", target / "sound.pkg")
    if args.voices != "None":
        copy_package(args.game_directory / "DATA" / f"voices{args.voices}.pkg", target / "voices.pkg")
    if not mapping_file.exists():
        mapping_file.write_bytes(encoded)
    if source.read_bytes() != raw:
        raise ValueError("Original configuration changed during preparation")
    print(f"Prepared {len(mappings)} sound mappings and private resource packages; voices={args.voices}")


if __name__ == "__main__":
    main()
