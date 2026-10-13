#!/usr/bin/env python3
"""Extract only selected test inputs from the contributor's own TRE files locally."""

from __future__ import annotations

import argparse
from pathlib import Path, PurePosixPath

from generate_local_animation_reference import ROOT, TreArchive, archive_paths


OUTPUT = ROOT / "tools/local-assets/tre_export"
PREFIXES = ("ui/", "string/en/", "texture/font/", "texture/ui_", "object/")


def safe_name(name: str) -> bool:
    path = PurePosixPath(name)
    return (name.startswith(PREFIXES) and not path.is_absolute()
            and not any(part in {"", ".", ".."} for part in path.parts)
            and "\\" not in name)


def extract(client_root: Path, output: Path = OUTPUT) -> tuple[int, int]:
    output = output.resolve()
    if not output.is_relative_to((ROOT / "tools/local-assets").resolve()):
        raise ValueError("Asset test inputs must stay inside ignored tools/local-assets")
    selected: dict[str, tuple[TreArchive, int]] = {}
    for path in archive_paths(client_root):
        archive = TreArchive(path)
        for name, (length, *_rest) in archive.entries.items():
            if safe_name(name) and name not in selected:
                selected[name] = (archive, length)
    if not selected:
        raise ValueError("No selected test inputs found in local TRE archives")
    total = 0
    for name, (archive, _length) in selected.items():
        target = output.joinpath(*PurePosixPath(name).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        data = archive.extract(name)
        temporary = target.with_suffix(target.suffix + ".tmp")
        temporary.write_bytes(data)
        temporary.replace(target)
        total += len(data)
    return len(selected), total


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client-root", type=Path, default=ROOT.parent)
    args = parser.parse_args()
    files, size = extract(args.client_root)
    print(f"Extracted {files} owned test inputs ({size // 1024 // 1024} MiB) into ignored tools/local-assets/")


if __name__ == "__main__":
    main()
