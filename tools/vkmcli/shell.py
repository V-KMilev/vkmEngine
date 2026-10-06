"""Running commands and talking to the person at the terminal."""

from __future__ import annotations

import contextlib
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path


def die(msg: str):
    print(f"vkm: {msg}", file=sys.stderr)
    sys.exit(1)


def say(msg: str):
    print(f"vkm: {msg}", flush=True)


# Set by -v: echo every command run, and all of its output.
VERBOSE = False


def show(cmd):
    """Print a command as it would be typed, to be rerun by hand."""
    words = [str(c) for c in cmd]
    quoted = words if os.name == "nt" else [shlex.quote(w) for w in words]
    print("+ " + " ".join(quoted), flush=True)


def run(cmd, **kw) -> int:
    """Run a command with its output on the terminal, and say so if it fails."""
    if VERBOSE:
        show(cmd)
    rc = subprocess.call([str(c) for c in cmd], **kw)
    if rc and not VERBOSE:
        say(f"{Path(str(cmd[0])).name} exited with code {rc}:")
        show(cmd)
    return rc


def capture(cmd) -> tuple[int, str]:
    """Run a command, holding its output for the caller to print or not."""
    if VERBOSE:
        show(cmd)
    done = subprocess.run(
        [str(c) for c in cmd],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        encoding="utf-8",
        errors="replace"
    )
    return done.returncode, done.stdout


EXE = ".exe" if os.name == "nt" else ""


def program(name: str) -> str | None:
    """The program called `name` on PATH, as this system can run it.

    On Windows, name.exe: a bare name may be a shell script (llvm-mingw's objdump).
    """
    return shutil.which(name + ".exe" if os.name == "nt" else name)


def platform_tag() -> str:
    """What a package runs on, as its name says: linux-x64, windows-x64."""
    system = "windows" if os.name == "nt" else re.sub(r"\d+$", "", sys.platform)
    machine = platform.machine().lower()
    arch = {"x86_64": "x64", "amd64": "x64", "aarch64": "arm64"}.get(machine, machine)
    return f"{system}-{arch}"


def size_of(path: Path) -> int:
    if path.is_file():
        return path.stat().st_size
    return sum(f.stat().st_size for f in path.rglob("*") if f.is_file() and not f.is_symlink())


def megabytes(n: int) -> str:
    return f"{n / 1e6:.1f} MB"


@contextlib.contextmanager
def step(label: str, measured: Path | None = None):
    """Print a package step with how long it took and what it wrote."""
    start = time.monotonic()
    print(f"  {label} ...", end="", flush=True)
    yield
    size = f", {megabytes(size_of(measured))}" if measured is not None and measured.exists() else ""
    print(f" {time.monotonic() - start:.1f} s{size}", flush=True)
