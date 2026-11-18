"""How vkm fetches a pinned tool, from a server on this machine: a file that fails its hash is refused and
nothing of it kept; a server's refusal is final; its failure or silence is tried again, up to the
attempts. Needs no engine build."""

import hashlib
import http.server
import io
import os
import select
import socket
import tarfile
import threading
import unittest

import _vkm

os.environ["VKM_TOOLS_DIR"] = str(_vkm.scratch("pinned_tools"))

from vkmcli import toolchain  # noqa: E402 - after the tools' folder is set

toolchain.DOWNLOAD_WAIT = 0.01
toolchain.DOWNLOAD_TIMEOUT = 0.2


def archive() -> bytes:
    """A tool's archive: one folder, one file in its bin/."""
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode="w:gz") as tar:
        data = b"#!/bin/sh\n"
        info = tarfile.TarInfo("fake-1/bin/fake")
        info.size = len(data)
        tar.addfile(info, io.BytesIO(data))
    return out.getvalue()


BLOB = archive()


class Server:
    """Answers `status` to the first `fails` requests, then serves BLOB. A status of 0 answers nothing
    and holds the request until the client lets go of it."""

    def __init__(self, fails: int, status: int):
        self.seen = 0
        self.abandoned = 0
        self.done = threading.Event()
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                owner.seen += 1
                if owner.seen <= fails:
                    if status == 0:
                        owner.hold(self.connection)
                        return
                    self.send_response(status)
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(BLOB)))
                self.end_headers()
                self.wfile.write(BLOB)

            def log_message(self, *args):
                pass

        # A thread a request, so one held holds up none after it.
        self.http = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.http.daemon_threads = True
        threading.Thread(target=self.http.serve_forever, daemon=True).start()

    def hold(self, connection):
        while not self.done.is_set():
            ready, _, _ = select.select([connection], [], [], 0.05)
            if ready and not connection.recv(1, socket.MSG_PEEK):
                self.abandoned += 1
                return

    def url(self) -> str:
        return f"http://127.0.0.1:{self.http.server_port}/fake.tar.gz"

    def close(self):
        self.done.set()
        self.http.shutdown()


def fetch(test: unittest.TestCase, name: str, fails: int, status: int, sha256: str = ""):
    """Fetch the tool `name` from a Server(fails, status); returns the tool and the server."""
    server = Server(fails, status)
    test.addCleanup(server.close)
    pin = {"version": "1", "url": server.url(), "sha256": sha256 or hashlib.sha256(BLOB).hexdigest(), "bin": "bin"}
    tool = toolchain.Tool(name, pin)
    # On a thread with a deadline: a fetch that hangs on a silent server fails here rather than never ends.
    worker = threading.Thread(target=lambda: _quietly(tool.fetch), daemon=True)
    worker.start()
    worker.join(60)
    test.assertFalse(worker.is_alive(), "the fetch hung")
    return tool, server


def _quietly(fn):
    try:
        fn()
    except SystemExit:
        pass


class PinnedTools(unittest.TestCase):
    def test_an_archive_that_fails_its_hash_is_refused_and_nothing_of_it_is_kept(self):
        tool, server = fetch(self, "forged", 0, 200, sha256="0" * 64)
        self.assertEqual(server.seen, 1)
        self.assertFalse(tool.present())
        self.assertFalse(tool.dir.exists())
        self.assertEqual([p.name for p in toolchain.tools_cache().iterdir() if "forged" in p.name or p.name.startswith("tmp")], [])

    def test_a_refusal_is_not_tried_again(self):
        tool, server = fetch(self, "missing", 1, 404)
        self.assertEqual(server.seen, 1)
        self.assertFalse(tool.present())

    def test_a_server_that_fails_is_tried_again_until_it_answers(self):
        tool, server = fetch(self, "flaky", toolchain.DOWNLOAD_ATTEMPTS - 1, 500)
        self.assertEqual(server.seen, toolchain.DOWNLOAD_ATTEMPTS)
        self.assertTrue(tool.present())
        self.assertTrue((tool.bin / "fake").is_file())

    def test_a_server_that_keeps_failing_is_given_up_on_after_the_attempts(self):
        tool, server = fetch(self, "down", toolchain.DOWNLOAD_ATTEMPTS, 503)
        self.assertEqual(server.seen, toolchain.DOWNLOAD_ATTEMPTS)
        self.assertFalse(tool.present())

    def test_a_server_that_says_nothing_is_let_go_and_tried_again(self):
        tool, server = fetch(self, "silent", toolchain.DOWNLOAD_ATTEMPTS - 1, 0)
        self.assertEqual(server.seen, toolchain.DOWNLOAD_ATTEMPTS)
        self.assertEqual(server.abandoned, toolchain.DOWNLOAD_ATTEMPTS - 1)
        self.assertTrue(tool.present())


if __name__ == "__main__":
    _vkm.main()
