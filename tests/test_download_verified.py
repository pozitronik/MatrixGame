# MatrixGame - licensed under GPLv2 or any later version.
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import threading
import unittest

PROJECT = Path(__file__).resolve().parents[1]
HELPER = PROJECT / "tools/download-verified.ps1"
GOOD = b"synthetic verified archive"
BAD = b"synthetic truncated or substituted response"


def sha256(data):
    return hashlib.sha256(data).hexdigest().upper()


class Responses(BaseHTTPRequestHandler):
    """Serves the queued (status, body) responses in order, then repeats the last one."""

    def do_GET(self):
        server = self.server
        server.requests += 1
        status, body = server.queue[min(server.requests, len(server.queue)) - 1]
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


class VerifiedDownloadTests(unittest.TestCase):
    def setUp(self):
        self.shell = shutil.which("pwsh") or shutil.which("powershell")
        if self.shell is None:
            self.skipTest("The supported Windows setup requires PowerShell")
        base = PROJECT / "build/setup-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=base)
        self.root = Path(self.temporary.name)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Responses)
        self.server.requests = 0
        self.server.queue = [(200, GOOD)]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.temporary.cleanup()

    def powershell(self, command):
        script = f". '{HELPER}'; $ErrorActionPreference = 'Stop'; {command}"
        return subprocess.run([self.shell, "-NoProfile", "-NonInteractive", "-Command", script],
                              capture_output=True, text=True, timeout=60)

    def download(self, target, attempts=3):
        url = f"http://127.0.0.1:{self.server.server_port}/archive.bin"
        return self.powershell(f"Save-VerifiedDownload -Uri '{url}' -Path '{target}' -Sha256 '{sha256(GOOD).lower()}' "
                               f"-Attempts {attempts} -RetryDelaySeconds 0")

    def assert_reported(self, text, result):
        # Windows PowerShell wraps redirected warnings at the console width.
        self.assertIn(text, " ".join((result.stdout + result.stderr).split()))

    def assert_no_partial(self, target):
        self.assertFalse(Path(str(target) + ".partial").exists())

    def test_verified_download_is_moved_into_place(self):
        target = self.root / "downloads/archive.bin"
        result = self.download(target)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(target.read_bytes(), GOOD)
        self.assertEqual(self.server.requests, 1)
        self.assert_no_partial(target)

    def test_mismatching_and_failed_responses_are_retried(self):
        self.server.queue = [(200, BAD), (503, b"unavailable"), (200, GOOD)]
        target = self.root / "archive.bin"
        result = self.download(target)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(target.read_bytes(), GOOD)
        self.assertEqual(self.server.requests, 3)
        self.assert_reported("SHA256 mismatch (received " + sha256(BAD) + ")", result)
        self.assert_no_partial(target)

    def test_exhausted_attempts_leave_no_file(self):
        self.server.queue = [(200, BAD)]
        target = self.root / "archive.bin"
        result = self.download(target, attempts=2)
        self.assertNotEqual(result.returncode, 0)
        self.assert_reported("Could not download a verified archive.bin after 2 attempts", result)
        self.assertEqual(self.server.requests, 2)
        self.assertFalse(target.exists())
        self.assert_no_partial(target)

    def test_matching_cached_file_is_reused_without_a_request(self):
        target = self.root / "archive.bin"
        target.write_bytes(GOOD)
        result = self.download(target)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.server.requests, 0)

    def test_corrupt_cached_file_is_replaced(self):
        target = self.root / "archive.bin"
        target.write_bytes(BAD)
        result = self.download(target)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_reported("Discarding cached archive.bin", result)
        self.assertEqual(target.read_bytes(), GOOD)
        self.assertEqual(self.server.requests, 1)

    def test_directory_hash_is_order_and_location_independent(self):
        tree = self.root / "tree"
        files = {"Include/d3d9.h": b"header", "Lib/x86/d3d9.lib": b"library", "Include/B.h": b"upper"}
        for relative, data in files.items():
            (tree / relative).parent.mkdir(parents=True, exist_ok=True)
            (tree / relative).write_bytes(data)
        lines = sorted(sha256(data) + " " + relative for relative, data in files.items())
        expected = hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest().upper()
        for root in (str(tree), str(tree) + "\\"):
            result = self.powershell(f"Get-DirectoryHash -Root '{root}'")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(), expected)
        (tree / "Include/B.h").write_bytes(b"changed")
        changed = self.powershell(f"Get-DirectoryHash -Root '{tree}'")
        self.assertNotEqual(changed.stdout.strip(), expected)


if __name__ == "__main__":
    unittest.main()
