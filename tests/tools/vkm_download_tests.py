"""How vkm fetches a pinned tool from a server that fails: it tries again after a server's own
failure, gives up after DOWNLOAD_ATTEMPTS, and never tries again after a refusal.

Run by CTest as `vkm_download`: python vkm_download_tests.py <the tools/ folder>.
"""
import hashlib
import http.server
import io
import os
import sys
import tarfile
import tempfile
import threading

sys.path.insert(0, sys.argv[1])
os.environ["VKM_TOOLS_DIR"] = tempfile.mkdtemp()

from vkmcli import toolchain  # noqa: E402 - after the path and the cache are set

toolchain.DOWNLOAD_WAIT = 0.01


def archive() -> bytes:
    """A tool's archive: one folder, one file in its bin/."""
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode="w:gz") as t:
        data = b"#!/bin/sh\n"
        info = tarfile.TarInfo("fake-1/bin/fake")
        info.size = len(data)
        t.addfile(info, io.BytesIO(data))
    return out.getvalue()


BLOB = archive()


def fetch(fails: int, status: int, name: str):
    """Fetch from a server that answers `status` `fails` times, then serves the archive.

    Returns whether the tool arrived, and how many requests the server saw.
    """
    seen = [0]

    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            seen[0] += 1
            if seen[0] <= fails:
                self.send_response(status)
                self.end_headers()
                return
            self.send_response(200)
            self.send_header("Content-Length", str(len(BLOB)))
            self.end_headers()
            self.wfile.write(BLOB)

        def log_message(self, *args):
            pass

    server = http.server.HTTPServer(("127.0.0.1", 0), Server)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    pin = {
        "version": "1",
        "url": f"http://127.0.0.1:{server.server_port}/fake.tar.gz",
        "sha256": hashlib.sha256(BLOB).hexdigest(),
        "bin": "bin",
    }
    tool = toolchain.Tool(name, pin)
    try:
        tool.fetch()
    except SystemExit:
        pass
    server.shutdown()
    return tool.present(), seen[0]


failures = 0


def check(what: str, ok: bool):
    global failures
    failures += not ok
    print(f"  {what:<62} {'ok' if ok else '<-- FAILED'}")


attempts = toolchain.DOWNLOAD_ATTEMPTS
arrived, seen = fetch(attempts - 1, 500, "flaky")
check("a server that fails, then answers, is tried until it does", arrived and seen == attempts)
arrived, seen = fetch(attempts, 503, "down")
check(f"  and given up on after {attempts} tries", not arrived and seen == attempts)
arrived, seen = fetch(1, 404, "missing")
check("a refusal is not tried again", not arrived and seen == 1)
print("ALL OK" if failures == 0 else f"{failures} FAILURE(S)")
sys.exit(1 if failures else 0)
