#!/usr/bin/env python3
"""Regenerate decoded animation rotations from the contributor's own client files.

The output is a local test reference, not repository content. This tool supports
the one executable revision whose decoder table was checked during development;
it refuses other revisions rather than producing plausible but unverified data.
"""

from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path
import re
import struct
import zlib


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "tools/local-assets/animation_rotations.tsv"
KNOWN_EXE_SHA256 = "58012e57cebc499454812ba7ed96b1289db01e520963b4fc364edb41c322b2a8"
DEFAULT_CLIPS = (
    "appearance/animation/all_b_idl_breathe_normally.ans",
    "appearance/animation/all_b_loc_walk_male.ans",
)
MAX_BLOCK = 128 * 1024 * 1024


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def inflate(data: bytes, expected: int) -> bytes:
    if not 0 < expected <= MAX_BLOCK:
        raise ValueError("Decompressed block size is outside the supported range")
    result = zlib.decompress(data)
    if len(result) != expected:
        raise ValueError("Decompressed block length differs from the archive index")
    return result


class TreArchive:
    def __init__(self, path: Path):
        self.path = path
        with path.open("rb") as stream:
            header = stream.read(36)
            if len(header) != 36:
                raise ValueError(f"Short TRE header: {path.name}")
            magic, version, count, offset, toc_codec, toc_size, name_codec, name_size, name_plain = struct.unpack("<9I", header)
            if (magic, version) != (0x54524545, 0x30303035):
                raise ValueError(f"Unsupported TRE header: {path.name}")
            if count * 24 > MAX_BLOCK or max(toc_size, name_size, name_plain) > MAX_BLOCK:
                raise ValueError(f"TRE index too large: {path.name}")
            stream.seek(offset)
            toc = stream.read(toc_size)
            names = stream.read(name_size)
            if len(toc) != toc_size or len(names) != name_size:
                raise ValueError(f"Truncated TRE index: {path.name}")
            toc = inflate(toc, count * 24) if toc_codec else toc
            names = inflate(names, name_plain) if name_codec else names
            if len(toc) != count * 24 or len(names) != name_plain:
                raise ValueError(f"TRE index length mismatch: {path.name}")
        self.entries: dict[str, tuple[int, int, int, int]] = {}
        for index in range(count):
            _, length, data_offset, codec, packed_size, name_offset = struct.unpack_from("<6I", toc, index * 24)
            if not length:
                continue
            if length > MAX_BLOCK or packed_size > MAX_BLOCK or name_offset >= len(names):
                raise ValueError(f"Invalid TRE entry: {path.name}")
            end = names.find(b"\0", name_offset)
            if end < 0:
                raise ValueError(f"Unterminated TRE name: {path.name}")
            name = names[name_offset:end].decode("utf-8")
            self.entries[name] = (length, data_offset, codec, packed_size)

    def extract(self, name: str) -> bytes:
        length, offset, codec, packed_size = self.entries[name]
        with self.path.open("rb") as stream:
            stream.seek(offset)
            data = stream.read(packed_size if codec else length)
        if len(data) != (packed_size if codec else length):
            raise ValueError(f"Truncated TRE entry: {self.path.name}:{name}")
        return inflate(data, length) if codec else data


def archive_paths(client_root: Path) -> list[Path]:
    config = client_root / "swgemu_live.cfg"
    if config.is_file():
        entries = []
        pattern = re.compile(r"searchTree_[0-9]+_([0-9]+)\s*=\s*([^\s]+\.tre)")
        for line in config.read_text(errors="replace").splitlines():
            match = pattern.search(line)
            if match:
                name = match.group(2)
                if Path(name).name != name:
                    raise ValueError("Archive config must name files beside the client")
                entries.append((int(match.group(1)), client_root / name))
        if entries:
            return [path for _, path in sorted(entries, key=lambda item: -item[0])]
        raise ValueError("swgemu_live.cfg has no archive searchTree entries")
    return sorted(client_root.glob("*.tre"))


def clip_bytes(client_root: Path, name: str) -> tuple[bytes, str]:
    if not name.startswith("appearance/animation/") or not name.endswith(".ans") or ".." in Path(name).parts:
        raise ValueError("Clip must be a relative appearance/animation/*.ans path")
    configured = (client_root / "swgemu_live.cfg").is_file()
    matches = []
    for path in archive_paths(client_root):
        archive = TreArchive(path)
        if name in archive.entries:
            if configured:
                return archive.extract(name), path.name
            matches.append(archive)
    if not matches:
        raise FileNotFoundError(f"Clip not found in local TRE archives: {name}")
    if len(matches) > 1:
        raise ValueError("Multiple archives contain this clip; use a client with swgemu_live.cfg to establish patch order")
    return matches[0].extract(name), matches[0].path.name


def chunks(data: bytes):
    pos = 0
    while pos < len(data):
        if len(data) - pos < 8:
            raise ValueError("Short IFF chunk")
        tag = data[pos:pos + 4].decode("ascii")
        size = struct.unpack_from(">I", data, pos + 4)[0]
        end = pos + 8 + size
        if end > len(data):
            raise ValueError("IFF chunk exceeds input")
        payload = data[pos + 8:end]
        if tag == "FORM":
            yield (tag, payload[:4].decode("ascii"), list(chunks(payload[4:])))
        else:
            yield (tag, payload)
        pos = end


def forms(nodes, kind: str):
    for node in nodes:
        if node[0] == "FORM":
            if node[1] == kind:
                yield node
            else:
                yield from forms(node[2], kind)


def leaves(nodes):
    for node in nodes:
        if node[0] == "FORM":
            yield from leaves(node[2])
        else:
            yield node


class Executable:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        if hashlib.sha256(self.data).hexdigest() != KNOWN_EXE_SHA256:
            raise ValueError("This SWGEmu.exe revision has not been verified for numeric rotation references")
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("Invalid PE header")
        count = struct.unpack_from("<H", self.data, pe + 6)[0]
        optional_size = struct.unpack_from("<H", self.data, pe + 20)[0]
        if struct.unpack_from("<H", self.data, pe + 24)[0] != 0x10B:
            raise ValueError("Expected the verified 32-bit PE format")
        self.base = struct.unpack_from("<I", self.data, pe + 52)[0]
        self.sections = [struct.unpack_from("<4I", self.data, pe + 24 + optional_size + 40 * i + 8)
                         for i in range(count)]

    def read(self, address: int, fmt: str):
        rva = address - self.base
        for virtual_size, start, raw_size, offset in self.sections:
            if start <= rva < start + max(virtual_size, raw_size):
                if rva - start + struct.calcsize(fmt) > raw_size:
                    break
                return struct.unpack_from(fmt, self.data, offset + rva - start)
        raise ValueError("Decoder table address is not mapped by this executable")


def context_table(exe: Executable):
    reciprocal_eleven = exe.read(0x15EAB50, "<f")[0]
    reciprocal_ten = exe.read(0x15EAB48, "<f")[0]
    one = exe.read(0x15DC6F8, "<f")[0]
    half = exe.read(0x15DC5E4, "<f")[0]
    numerator = exe.read(0x15DD92C, "<f")[0]
    result = {}
    for level in range(7):
        prefix, count, step = exe.read(0x18AF640 + level * 28, "<IIf")
        if count != 1 << level:
            raise ValueError("Unexpected decoder context table")
        half_range = numerator / (count + 1) * half
        eleven = f32(half_range * reciprocal_eleven)
        ten = f32(half_range * reciprocal_ten)
        for index in range(count):
            context = (prefix & 255) | index
            result[context] = (f32((index + 1) * step - one), eleven, ten)
    if len(result) != 127:
        raise ValueError("Incomplete decoder context table")
    return result


def rotation(contexts: bytes, packed: int, table) -> tuple[float, float, float, float]:
    values = []
    z_extended = 0.0
    for axis, shift, magnitude_mask, sign_mask in ((0, 21, 0x3FF, 0x400),
                                                    (1, 10, 0x3FF, 0x400),
                                                    (2, 0, 0x1FF, 0x200)):
        base, eleven, ten = table[contexts[axis]]
        field = packed >> shift
        increment = (field & magnitude_mask) * (ten if axis == 2 else eleven)
        value = base - increment if field & sign_mask else base + increment
        if axis == 2:
            z_extended = value
        values.append(f32(value))
    x, y, z = values
    residual = 1 - x * x - y * y - z_extended * z_extended
    if residual < 0:
        raise ValueError("Invalid packed quaternion for the verified decoder")
    return f32(math.sqrt(residual)), x, y, z


def clip_rows(name: str, data: bytes, table) -> list[str]:
    root = list(chunks(data))
    version = next(forms(root, "0001"))
    nested = {node[1] if node[0] == "FORM" else node[0]: node for node in version[2]}
    flat = {node[0]: node[1] for node in leaves(root)}
    animated = []
    for node in nested["AROT"][2]:
        if node[0] != "QCHN":
            raise ValueError("Unexpected animated rotation channel")
        payload = node[1]
        count = struct.unpack_from("<H", payload)[0]
        if len(payload) != 5 + count * 6:
            raise ValueError("Animated rotation channel length mismatch")
        animated.append([(frame, rotation(payload[2:5], packed, table))
                         for frame, packed in struct.iter_unpack("<HI", payload[5:])])
    static_payload = flat.get("SROT", b"")
    if len(static_payload) % 7:
        raise ValueError("Static rotation channel length mismatch")
    fixed = [rotation(static_payload[pos:pos + 3], struct.unpack_from("<I", static_payload, pos + 3)[0], table)
             for pos in range(0, len(static_payload), 7)]
    lines = [f"# {name} sha256={hashlib.sha256(data).hexdigest()}"]
    for node in nested["XFRM"][2]:
        if node[0] != "XFIN":
            raise ValueError("Unexpected transform channel")
        payload = node[1]
        zero = payload.index(0)
        bone = payload[:zero].decode("ascii")
        is_animated, index, *_ = struct.unpack("<BHB3H", payload[zero + 1:])
        if index == 0xFFFF:
            continue
        keys = animated[index] if is_animated else [(-1, fixed[index])]
        for frame, quaternion in keys:
            lines.append(" ".join((Path(name).stem, bone, str(frame),
                                   *(format(value, ".17g") for value in quaternion))))
    return lines


def generate(client_root: Path, clips: tuple[str, ...], output: Path) -> int:
    output = output.resolve()
    if not output.is_relative_to((ROOT / "tools/local-assets").resolve()):
        raise ValueError("Output must stay inside the repository's ignored tools/local-assets directory")
    table = context_table(Executable(client_root / "SWGEmu.exe"))
    lines = ["# Local reference generated from contributor-owned client files; do not commit this file.",
             f"# executable sha256={KNOWN_EXE_SHA256}"]
    for clip in clips:
        data, archive = clip_bytes(client_root, clip)
        lines.append(f"# archive={archive}")
        lines.extend(clip_rows(clip, data, table))
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text("\n".join(lines) + "\n")
    temporary.replace(output)
    return sum(not line.startswith("#") for line in lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client-root", type=Path, default=ROOT.parent)
    parser.add_argument("--clip", action="append", dest="clips", default=None,
                        help="relative appearance/animation/*.ans path; may be repeated")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    rows = generate(args.client_root, tuple(args.clips or DEFAULT_CLIPS), args.output)
    print(f"Generated {rows} local rotation rows in ignored tools/local-assets/")


if __name__ == "__main__":
    main()
