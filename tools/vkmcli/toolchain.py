"""The tools an SDK builds with, pinned in toolchain.json and fetched once per user."""

from __future__ import annotations

import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

from .engine import EngineBuild, TOOL_DIR
from .shell import EXE, die, megabytes, note, platform_tag


# The tools an SDK builds and runs with, pinned per platform: tools/toolchain.json,
# installed beside vkm.py. CI builds the SDK with the same file, so a module and the
# engine come from one compiler.
TOOLCHAIN_FILE = TOOL_DIR / "toolchain.json"


# What a build needs; the pinned Python ships inside the SDK instead.
BUILD_TOOLS = ("gcc", "cmake", "ninja")


def tools_cache() -> Path:
    """Where pinned tools are unpacked, once per user and shared by every SDK: VKM_TOOLS_DIR overrides."""
    if env := os.environ.get("VKM_TOOLS_DIR"):
        return Path(env)
    if os.name == "nt":
        return Path(os.environ.get("LOCALAPPDATA") or Path.home() / "AppData" / "Local") / "vkm" / "tools"
    return Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "vkm" / "tools"


class Tool:
    """One pinned tool: an archive at a URL with a known SHA-256, unpacked under tools_cache()."""

    def __init__(self, name: str, pin: dict):
        self.name = name
        self.version = pin["version"]
        self.url = pin["url"]
        self.sha256 = pin["sha256"]
        self.dir = tools_cache() / f"{name}-{self.version}"
        self.bin = self.dir / pin["bin"]
        self.marker = self.dir / ".vkm-sha256"

    def present(self) -> bool:
        return self.marker.is_file() and self.marker.read_text().strip() == self.sha256

    def fetch(self):
        """Download, verify and unpack the tool; an unpacked one is kept only once it verified."""
        import hashlib
        import tarfile
        import urllib.request
        import zipfile

        cache = tools_cache()
        cache.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=cache) as tmp:
            archive = Path(tmp) / self.url.rsplit("/", 1)[-1]
            note(f"vkm: fetching {self.name} {self.version}, once for every project")
            digest = hashlib.sha256()
            try:
                with urllib.request.urlopen(self.url) as src, open(archive, "wb") as dst:
                    total = int(src.headers.get("Content-Length") or 0)
                    done = 0
                    shown = -1
                    while chunk := src.read(1 << 20):
                        dst.write(chunk)
                        digest.update(chunk)
                        done += len(chunk)
                        if total and sys.stderr.isatty() and done * 10 // total != shown:
                            shown = done * 10 // total
                            note(f"\r  {megabytes(done)} of {megabytes(total)}", end="")
                if total and sys.stderr.isatty():
                    note("")
            except OSError as e:
                die(f"could not download {self.url}: {e}")
            if digest.hexdigest() != self.sha256:
                got = digest.hexdigest()
                die(f"{archive.name} is not the file {TOOLCHAIN_FILE.name} pins; its SHA-256 is {got}")

            unpacked = Path(tmp) / "unpacked"
            if archive.name.endswith(".zip"):
                with zipfile.ZipFile(archive) as z:
                    z.extractall(unpacked)
            else:
                with tarfile.open(archive) as t:
                    # The tar filter refuses paths that leave the folder, where Python has one. Not
                    # the data filter: Ubuntu's 3.10 misreads the pinned Python's relative links,
                    # and the hash above already vouches for what they point at.
                    if hasattr(tarfile, "tar_filter"):
                        t.extractall(unpacked, filter="tar")
                    else:
                        t.extractall(unpacked)

            # An archive that holds one folder is that folder.
            entries = list(unpacked.iterdir())
            root = entries[0] if len(entries) == 1 and entries[0].is_dir() else unpacked
            # A zip keeps no executable bits.
            if os.name != "nt":
                for f in (root / self.bin.relative_to(self.dir)).iterdir():
                    if f.is_file():
                        f.chmod(f.stat().st_mode | 0o111)
            if self.dir.exists():
                shutil.rmtree(self.dir)
            shutil.move(str(root), str(self.dir))
            self.marker.write_text(self.sha256 + "\n")


def pinned_tools(names=BUILD_TOOLS) -> list[Tool]:
    """This platform's pinned tools called `names`, in that order."""
    pins = json.loads(TOOLCHAIN_FILE.read_text()).get(platform_tag(), {}) if TOOLCHAIN_FILE.is_file() else {}
    unknown = [n for n in names if n not in pins]
    if unknown:
        die(f"{TOOLCHAIN_FILE} pins no {', '.join(unknown)} for {platform_tag()}")
    return [Tool(name, pins[name]) for name in names]


def tools_on_path(tools: list[Tool]):
    """Fetch whatever of `tools` is missing and put every bin/ first on PATH, in order."""
    for tool in tools:
        if not tool.present():
            tool.fetch()
    dirs = [str(t.bin) for t in tools]
    rest = [p for p in os.environ.get("PATH", "").split(os.pathsep) if p and p not in dirs]
    os.environ["PATH"] = os.pathsep.join(dirs + rest)


def build_tools(engine: EngineBuild) -> list[Tool]:
    """What a module builds with against `engine`: the pinned CMake and Ninja, and its GCC if pinned."""
    return pinned_tools(("cmake", "ninja") + (("gcc",) if engine.pinned() else ()))


def module_compiler(engine: EngineBuild) -> str | None:
    """The C++ compiler a project's module is built with when nothing names one.

    The pinned GCC for an engine built with it, on any machine; else the compiler
    that built the engine, as its package records it.
    """
    if engine.pinned():
        return str(pinned_tools(("gcc",))[0].bin / ("g++" + EXE))
    return engine.compiler()


def cmd_toolchain(args) -> int:
    """Fetch pinned tools (the build's, unless named) and say where each is."""
    tools = pinned_tools(tuple(args.tools) or BUILD_TOOLS)
    for tool in tools:
        if not tool.present():
            tool.fetch()
    for tool in tools:
        if args.path:
            print(tool.bin)
        elif args.dir:
            print(tool.dir)
        else:
            print(f"  {tool.name:<6} {tool.version:<8} {tool.dir}")
    return 0
