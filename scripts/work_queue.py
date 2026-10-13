#!/usr/bin/env python3
"""Recommend unclaimed issues using public GitHub metadata, with an offline fallback."""

import argparse
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import re
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
API = "https://api.github.com/repos/TrigsC/SWG-64bit"
PAGE_SIZE = 100
CLAIM_DAYS = 14
HEADERS = {"Accept": "application/vnd.github+json", "User-Agent": "swg-64bit-work-queue"}
ISSUE_REF = re.compile(r"(?:#|/issues/)(\d+)\b")
CLAIM = re.compile(r"\b(?:i(?:'m| am) working on|i(?:'ll| will) work on|claiming (?:this|issue|#\d+)|taking this)\b", re.I)
RELEASE = re.compile(r"\b(?:unclaim(?:ing)?|no longer working on|releas(?:e|ing) (?:this|the) issue|available again)\b", re.I)


def fetch_pages(path: str) -> list[dict]:
    """Read all REST pages; GitHub's issue endpoint includes pull requests."""
    items = []
    page = 1
    while True:
        separator = "&" if "?" in path else "?"
        request = urllib.request.Request(
            f"{API}{path}{separator}per_page={PAGE_SIZE}&page={page}", headers=HEADERS
        )
        with urllib.request.urlopen(request, timeout=10) as response:
            batch = json.load(response)
        if not isinstance(batch, list):
            raise ValueError("GitHub returned a non-list response")
        items.extend(batch)
        if len(batch) < PAGE_SIZE:
            return items
        page += 1


def labels_for(issue: dict) -> set[str]:
    return {label["name"].lower() for label in issue.get("labels", [])}


def field(body: str, name: str) -> str | None:
    """Read a Markdown metadata field, including fields separated by middots."""
    match = re.search(rf"(?:^|[·\n])\s*(?:\*\*)?{name}:(?:\*\*)?\s*([^·\n]+)", body, re.I)
    return match.group(1).strip(" *") if match else None


def metadata(issue: dict) -> tuple[str, str, set[int]]:
    body = issue.get("body") or ""
    labels = labels_for(issue)
    platform = (field(body, "Platform") or "unspecified").lower()
    needs = field(body, "(?:Needs|Prerequisites?)") or "none listed"
    if "needs-windows-reference" in labels and "Windows reference" not in needs:
        needs = "Windows reference" if needs == "none listed" else f"{needs}; Windows reference"
    if "needs-assets" in labels and "local assets" not in needs:
        needs = "local assets" if needs == "none listed" else f"{needs}; local assets"
    depends = {int(number) for number in ISSUE_REF.findall(field(body, "Depends on") or "")}
    return platform, needs, depends


def current_claim(comments: list[dict], now: datetime) -> bool:
    """A recent explicit claim lasts 14 days; a later release cancels it."""
    cutoff = now - timedelta(days=CLAIM_DAYS)
    state = False
    for comment in sorted(comments, key=lambda entry: entry.get("created_at", "")):
        timestamp = comment.get("created_at", "")
        try:
            created = datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
        except ValueError:
            continue
        if created < cutoff:
            continue
        body = comment.get("body") or ""
        if RELEASE.search(body):
            state = False
        elif CLAIM.search(body):
            state = True
    return state


def matches_platform(issue: dict, choice: str, open_numbers: set[int]) -> bool:
    platform, _needs, depends = metadata(issue)
    labels = labels_for(issue)
    if depends & open_numbers:
        return False
    if choice == "all":
        return True
    priority = (field(issue.get("body") or "", "Priority") or "").lower()
    research = "research" in labels or "research" in platform or "research" in priority or "planning" in priority
    if choice == "research":
        return research
    if research:
        return False
    if platform == "unspecified":
        return False
    windows = "windows" in platform or "needs-windows-reference" in labels
    mac = "mac" in platform or "arm64" in platform or "silicon" in platform
    if choice == "mac":
        return not windows or (mac and "optional" in platform and "needs-windows-reference" not in labels)
    return not mac or windows


def candidates(issues: list[dict], pulls: list[dict], choice: str, now: datetime) -> list[dict]:
    open_numbers = {issue["number"] for issue in issues if "pull_request" not in issue}
    linked = {
        int(number)
        for pull in pulls
        for number in ISSUE_REF.findall(pull.get("body") or "")
    }
    result = []
    for issue in issues:
        if "pull_request" in issue or issue.get("assignee") or issue.get("assignees"):
            continue
        if issue["number"] in linked:
            continue
        if labels_for(issue) & {"claimed", "in-progress", "blocked"}:
            continue
        if not matches_platform(issue, choice, open_numbers):
            continue
        if issue.get("comments") and current_claim(fetch_pages(f"/issues/{issue['number']}/comments"), now):
            continue
        result.append(issue)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("mac", "windows", "research", "all"), default="mac",
                        help="Issue filter (default: mac)")
    parser.add_argument("--limit", type=int, default=8, help="Maximum recommendations (default: 8)")
    args = parser.parse_args()
    if args.limit < 1:
        parser.error("--limit must be positive")
    try:
        issues = fetch_pages("/issues?state=open")
        pulls = fetch_pages("/pulls?state=open")
        available = candidates(issues, pulls, args.platform, datetime.now(timezone.utc))
    except (urllib.error.URLError, ValueError, KeyError) as error:
        print(f"GitHub work queue unavailable ({error}); read {ROOT / 'WORK_QUEUE.md'}.")
        return
    if not available:
        print(f"No unclaimed {args.platform} issues. Read {ROOT / 'WORK_QUEUE.md'} and inspect open PRs before starting.")
        return
    for issue in available[:args.limit]:
        platform, needs, depends = metadata(issue)
        prerequisite = f"; depends on {', '.join(f'#{number}' for number in sorted(depends))}" if depends else ""
        print(f"#{issue['number']} {issue['title']} [platform: {platform}; needs: {needs}{prerequisite}] — {issue['html_url']}")
    print(f"Claims in comments expire after {CLAIM_DAYS} days. Check the issue and open PRs before starting.")


if __name__ == "__main__":
    main()
