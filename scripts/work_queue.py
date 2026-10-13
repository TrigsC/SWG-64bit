#!/usr/bin/env python3
"""Show unclaimed public GitHub issues; fall back to WORK_QUEUE.md offline."""

from pathlib import Path
import json
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
URL = "https://api.github.com/repos/TrigsC/SWG-64bit/issues?state=open&per_page=100"


def main() -> None:
    request = urllib.request.Request(URL, headers={"Accept": "application/vnd.github+json", "User-Agent": "swg-64bit-work-queue"})
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            issues = json.load(response)
    except (urllib.error.URLError, ValueError) as error:
        print(f"GitHub work queue unavailable ({error}); read {ROOT / 'WORK_QUEUE.md'}.")
        return
    available = []
    for issue in issues:
        if "pull_request" in issue or issue.get("assignee"):
            continue
        labels = {label["name"].lower() for label in issue.get("labels", [])}
        if "claimed" in labels or "in-progress" in labels or "blocked" in labels:
            continue
        available.append(issue)
    if not available:
        print(f"No unassigned open issues. Read {ROOT / 'WORK_QUEUE.md'} and inspect open PRs before starting.")
        return
    for issue in available[:8]:
        print(f"#{issue['number']} {issue['title']} — {issue['html_url']}")
    print("Check comments and open PRs before claiming any issue.")


if __name__ == "__main__":
    main()
