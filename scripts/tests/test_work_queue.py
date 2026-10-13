"""Synthetic GitHub responses for the public contributor work queue."""

from datetime import datetime, timedelta, timezone
import importlib.util
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch
import urllib.error

SPEC = importlib.util.spec_from_file_location("work_queue", Path(__file__).resolve().parents[1] / "work_queue.py")
queue = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(queue)
NOW = datetime(2026, 10, 13, tzinfo=timezone.utc)


def issue(number, platform="any", labels=(), comments=0, extra=""):
    return {
        "number": number, "title": f"Issue {number}", "html_url": f"https://github.com/TrigsC/SWG-64bit/issues/{number}",
        "body": f"**Platform:** {platform}\n{extra}", "labels": [{"name": name} for name in labels],
        "comments": comments, "assignee": None,
    }


class Response:
    def __init__(self, items):
        self.body = io.BytesIO(json.dumps(items).encode())

    def __enter__(self):
        return self.body

    def __exit__(self, *_):
        self.body.close()


class WorkQueueTests(unittest.TestCase):
    def test_paginates_issues_and_pulls(self):
        with patch.object(queue, "PAGE_SIZE", 2), patch.object(
            queue.urllib.request, "urlopen",
            side_effect=[Response([issue(1), issue(2)]), Response([issue(3)]),
                         Response([{"body": "Closes #2"}, {"body": "Other work"}]), Response([])],
        ) as get:
            issues = queue.fetch_pages("/issues?state=open")
            pulls = queue.fetch_pages("/pulls?state=open")
        self.assertEqual([item["number"] for item in issues], [1, 2, 3])
        self.assertEqual(len(pulls), 2)
        self.assertIn("page=2", get.call_args_list[1].args[0].full_url)
        self.assertIn("page=2", get.call_args_list[3].args[0].full_url)

    def test_filters_platform_dependencies_labels_assignments_and_linked_prs(self):
        rows = [
            issue(1, "macOS ARM64"), issue(2, "Windows x64"), issue(3, "research", ["research"]),
            issue(4, "any"), issue(5, "Mac", extra="**Depends on:** #4"),
            issue(6, "any", ["needs-windows-reference"]), issue(7, "any", ["blocked"]),
            issue(8, "any"), issue(9, "any"), issue(10, "unknown"),
        ]
        rows[7]["assignee"] = {"login": "worker"}
        rows[9]["body"] = "No structured platform metadata"
        pulls = [{"body": "Fixes #9"}]
        with patch.object(queue, "fetch_pages", return_value=[]):
            self.assertEqual([row["number"] for row in queue.candidates(rows, pulls, "mac", NOW)], [1, 4])
            self.assertEqual([row["number"] for row in queue.candidates(rows, pulls, "windows", NOW)], [2, 4, 6])
            self.assertEqual([row["number"] for row in queue.candidates(rows, pulls, "research", NOW)], [3])

    def test_recent_claim_expires_and_release_cancels(self):
        def comment(days, body):
            return {"created_at": (NOW - timedelta(days=days)).isoformat(), "body": body}
        self.assertTrue(queue.current_claim([comment(2, "I am working on this")], NOW))
        self.assertFalse(queue.current_claim([comment(20, "Claiming this issue")], NOW))
        self.assertFalse(queue.current_claim([comment(2, "I'm working on this"), comment(1, "available again")], NOW))
        self.assertFalse(queue.current_claim([comment(1, "Claims should expire")], NOW))

    def test_comments_are_checked_only_for_remaining_candidates(self):
        rows = [issue(1, "mac", comments=1), issue(2, "windows", comments=1)]
        with patch.object(queue, "fetch_pages", return_value=[
            {"created_at": NOW.isoformat(), "body": "Taking this"}
        ]) as fetch:
            self.assertEqual(queue.candidates(rows, [], "mac", NOW), [])
            fetch.assert_called_once_with("/issues/1/comments")

    def test_bad_github_response_and_unavailable_network(self):
        with patch.object(queue.urllib.request, "urlopen", return_value=Response({"message": "rate limit"})):
            with self.assertRaises(ValueError):
                queue.fetch_pages("/issues?state=open")
        with patch.object(queue.urllib.request, "urlopen", side_effect=urllib.error.URLError("offline")):
            with self.assertRaises(urllib.error.URLError):
                queue.fetch_pages("/issues?state=open")

    def test_reports_metadata(self):
        row = issue(42, "macOS ARM64", ["needs-assets"], extra="**Needs:** controlled server\n**Depends on:** #12")
        self.assertEqual(queue.metadata(row), ("macos arm64", "controlled server; local assets", {12}))
        row["body"] = "**Priority:** later gameplay · **Platform:** macOS ARM64 · **Depends on:** #17 and #18"
        self.assertEqual(queue.metadata(row), ("macos arm64", "local assets", {17, 18}))


if __name__ == "__main__":
    unittest.main()
