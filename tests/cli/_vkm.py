"""What the tests of the project tool share: the tool, the engine build it drives, and a folder each.

A test file is a ctest of its own (tests/CMakeLists.txt): `main()` runs its tests and exits 0, 1 when
one failed, or 77 when this machine cannot run them.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[2]
TOOLS = ENGINE / "tools"
VKM = TOOLS / "vkm.py"

# The build ctest runs from, and where its tests write.
BUILD = Path(os.environ.get("VKM_TEST_BUILD", ENGINE / "build")).resolve()
SCRATCH = Path(os.environ.get("VKM_TEST_SCRATCH", BUILD / "test_scratch" / "cli"))

sys.path.insert(0, str(TOOLS))


def skip(reason: str):
    """Stop: this machine cannot run the file's tests, which ctest shows as skipped."""
    print(f"SKIPPED: {reason}")
    sys.exit(77)


def needs_the_build_vkm_drives():
    """vkm drives the engine tree's build/; from any other build its result says nothing of this one."""
    if BUILD != (ENGINE / "build").resolve():
        skip(f"vkm drives {ENGINE / 'build'}, and this is {BUILD}")


def scratch(name: str) -> Path:
    """A folder of the test's own, empty."""
    folder = SCRATCH / name
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    return folder


def vkm(*args, cwd: Path | None = None) -> subprocess.CompletedProcess:
    """Run vkm as a user does, its output (both streams) in .stdout."""
    return subprocess.run(
        [sys.executable, str(VKM), *map(str, args)],
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True
    )


def main():
    """Run the calling file's tests, each named by what it proves."""
    program = unittest.main(module="__main__", exit=False, verbosity=2)
    sys.exit(0 if program.result.wasSuccessful() else 1)
