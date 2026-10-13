#!/usr/bin/env python3
"""Index names from the contributor's own TRE archives; keep the result local."""

from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT.parent
OUTPUT = ROOT / "tools/local-assets/object_template_candidates.txt"
MAX_BLOCK = 128 * 1024 * 1024


def archive_names(path: Path):
    with path.open("rb") as stream:
        header = stream.read(36)
        if len(header) != 36:
            raise ValueError("short TRE header")
        magic, version, count, offset, toc_codec, toc_size, names_codec, names_size, names_plain_size = struct.unpack("<9I", header)
        if magic != 0x54524545 or version != 0x30303035:
            raise ValueError("unsupported TRE header")
        if count * 24 > MAX_BLOCK or max(toc_size, names_size, names_plain_size) > MAX_BLOCK:
            raise ValueError("TRE index is too large")
        stream.seek(offset)
        toc = stream.read(toc_size)
        names = stream.read(names_size)
        if len(toc) != toc_size or len(names) != names_size:
            raise ValueError("truncated TRE index")
        if toc_codec:
            toc = zlib.decompress(toc)
        if names_codec:
            names = zlib.decompress(names)
        if len(toc) != count * 24 or len(names) != names_plain_size:
            raise ValueError("TRE index size mismatch")
        for i in range(count):
            _, length, _, _, _, name_offset = struct.unpack_from("<6I", toc, i * 24)
            if length == 0:
                continue
            if name_offset >= len(names):
                raise ValueError("TRE name offset out of range")
            end = names.find(b"\0", name_offset)
            if end < 0:
                raise ValueError("unterminated TRE name")
            try:
                yield names[name_offset:end].decode("utf-8")
            except UnicodeDecodeError:
                continue


def main() -> None:
    archives = sorted(GAME.glob("*.tre"))
    if not archives:
        raise SystemExit("No .tre archives found beside the modernization folder.")
    candidates = set()
    for archive in archives:
        try:
            for name in archive_names(archive):
                if not name.startswith("object/") or not name.endswith(".iff"):
                    continue
                directory, _, basename = name.rpartition("/")
                if basename.startswith("shared_"):
                    candidates.add(directory + "/" + basename.removeprefix("shared_"))
        except (OSError, ValueError, zlib.error) as error:
            raise SystemExit(f"Cannot index {archive.name}: {error}") from error
    if not candidates:
        raise SystemExit("No shared object templates found in local .tre archives.")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    temp = OUTPUT.with_suffix(".tmp")
    temp.write_text("\n".join(sorted(candidates)) + "\n")
    temp.replace(OUTPUT)
    print(f"Indexed {len(candidates)} local object templates from {len(archives)} archives; index stays in ignored tools/.")


if __name__ == "__main__":
    main()
