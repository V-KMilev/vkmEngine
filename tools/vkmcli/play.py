"""Running a project: alone, in the editor, served, or with players."""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import time
from pathlib import Path

from . import shell
from .build import build_module, up_to_date
from .engine import development
from .project import project_dir, project_name
from .shell import die, run, say, show


def cmd_edit(args) -> int:
    """Open the editor on a project, built first; a failed build still opens it.

    The editor reloads the module when the next build lands, and cooks as it saves.
    """
    d = project_dir(args.project)
    if build_module(d, development(), jobs=args.jobs):
        say("the build failed; opening the editor on the module built last")
    return run([development().host("vkm_editor"), d])


def cmd_run(args) -> int:
    if args.connect and args.players:
        die("--connect joins a game served elsewhere and --players serves one here; name one of them")
    if args.port and not args.players:
        die("--port is where --players serves; with --connect, give it as HOST:PORT")

    d = project_dir(args.project)
    if rc := up_to_date(d, args.jobs):
        return rc
    if args.players:
        return play_together(d, args.players, args.port)
    return run([development().host("vkm_runtime"), d, *(["--connect", args.connect] if args.connect else [])])


def cmd_serve(args) -> int:
    d = project_dir(args.project)
    if rc := up_to_date(d, args.jobs):
        return rc
    return run([development().host("vkm_server"), d, *(["--port", str(args.port)] if args.port else [])])


def play_together(d: Path, players: int, port: int | None) -> int:
    """Serve a project and start `players` clients against it, from one terminal."""
    engine = development()
    server_exe = engine.host("vkm_server")
    client_exe = engine.host("vkm_runtime")

    address = f"127.0.0.1:{port}" if port else "127.0.0.1"
    serve_args = ["--port", str(port)] if port else []

    say(f"serving {project_name(d)} and starting {players} player(s) against it; close any window to end it")
    # A kill exits through the cleanup below, closing every window opened.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    children = []
    try:
        if shell.VERBOSE:
            show([server_exe, d, *serve_args])
        children.append(subprocess.Popen([str(server_exe), str(d), *serve_args]))

        # A head start well inside the silence a client waits out (NetConnection::TIMEOUT_SECONDS).
        time.sleep(1.0)
        if children[0].poll() is not None:
            die(f"the server exited before any player started (code {children[0].returncode})")

        for n in range(1, players + 1):
            client = [client_exe, d, "--connect", address]
            if shell.VERBOSE:
                show(client)
            # A log file of its own each.
            env = {**os.environ, "VKM_LOG_SUFFIX": str(n)}
            children.append(subprocess.Popen([str(c) for c in client], env=env))

        # Ends when any of them does.
        while True:
            for child in children:
                if child.poll() is not None:
                    return child.returncode
            time.sleep(0.2)
    except KeyboardInterrupt:
        return 0
    finally:
        # Clients first, so the server sees them leave rather than time out.
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
        for child in children:
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
