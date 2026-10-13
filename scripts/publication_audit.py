#!/usr/bin/env python3
"""Flag material that needs review before a public source release.

This is a conservative gate, not a legal or privacy clearance. It scans only
tracked files and prints locations, never the matching source text.
"""

from __future__ import annotations

import argparse
from collections import Counter
import ipaddress
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
BLOCKED_EXTENSIONS = {
    ".tre", ".exe", ".dll", ".dmp", ".mdmp", ".pcap", ".pcapng",
    ".tsv", ".hex", ".db", ".sqlite", ".png", ".jpg", ".jpeg",
    ".dds", ".tga", ".iff", ".skb", ".skt", ".ckat", ".jsonl",
}
BLOCKED_PARTS = {"animation-audit", "tre_export", "diagnostic_screenshots"}
REVIEW_PATTERNS = {
    "local_path": re.compile(r"(?:/Users/[^/\s]+|[A-Za-z]:\\Users\\[^\\\s]+)"),
    "network_address": re.compile(r"\b(?:\d{1,3}\.){3}\d{1,3}\b"),
    "embedded_wire_bytes": re.compile(r"(?:\b[0-9a-fA-F]{2}[ ,]){12,}"),
    "binary_research_note": re.compile(
        r"(?i)(?:SWGEmu\.exe.{0,100}(?:sha-?256|0x[0-9a-f]{6,})|"
        r"\b(?:x32dbg|RenderDoc|decompiled|binary address|original executable)\b)"
    ),
    "session_research_note": re.compile(
        r"(?i)\b(?:real (?:sample|capture|session)|recorded (?:packet|movement|"
        r"animation)|private server|user report|observed in (?:our|the) (?:session|capture))\b"
    ),
}
TEXT_EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hpp", ".md", ".py", ".sh", ".swift", ".hlsl", ".json", ".yml", ".yaml", ".txt"}
REVIEW_ONLY = {"binary_research_note", "session_research_note"}


def tracked_paths() -> list[Path]:
    raw = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT)
    return [Path(p.decode("utf-8", "surrogateescape")) for p in raw.split(b"\0") if p]


def audit() -> list[tuple[str, str, int]]:
    findings: list[tuple[str, str, int]] = []
    for relative in tracked_paths():
        name = relative.as_posix()
        if name == "scripts/publication_audit.py":
            continue
        if relative.suffix.lower() in BLOCKED_EXTENSIONS or any(p in BLOCKED_PARTS for p in relative.parts):
            findings.append(("blocked_file", name, 0))
            continue
        if relative.suffix.lower() not in TEXT_EXTENSIONS and relative.name not in {"CMakeLists.txt", "CMakePresets.json", ".gitignore", "AGENTS.md"}:
            continue
        try:
            lines = (ROOT / relative).read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeError):
            findings.append(("unreadable_text", name, 0))
            continue
        for number, line in enumerate(lines, 1):
            for category, pattern in REVIEW_PATTERNS.items():
                if category in {"binary_research_note", "session_research_note"} and relative.suffix.lower() == ".md":
                    continue
                if category == "network_address":
                    addresses = []
                    for match in pattern.finditer(line):
                        if line[max(0, match.start() - 2):match.start()] == "==":
                            continue  # A pinned package version, not an address.
                        try:
                            address = ipaddress.IPv4Address(match.group())
                        except ipaddress.AddressValueError:
                            continue
                        if not address.is_loopback:
                            addresses.append(address)
                    if not addresses:
                        continue
                elif not pattern.search(line):
                    continue
                findings.append((category, name, number))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", action="store_true", help="show counts only")
    args = parser.parse_args()
    findings = audit()
    counts = Counter(category for category, _, _ in findings)
    print(f"Tracked files: {len(tracked_paths())}; review findings: {len(findings)}")
    for category, count in sorted(counts.items()):
        print(f"  {category}: {count}")
    if not args.summary:
        for category, path, line in findings:
            print(f"{category}: {path}{':' + str(line) if line else ''}")
    blocking = [finding for finding in findings if finding[0] not in REVIEW_ONLY]
    if blocking:
        print(f"Publication gate FAILED: {len(blocking)} blocked finding(s); research-note matches also require human review.")
        return 1
    print(f"Automated file/content checks passed; {len(findings)} research-note match(es) are advisory. Manual provenance, license, and privacy review is still required.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
