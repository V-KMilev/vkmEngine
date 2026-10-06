"""Checking the engine, the toolchain and the project."""

from __future__ import annotations

import os
import platform
import shutil
import sys
from pathlib import Path

from .engine import MODULE, development, in_engine_tree, sdk_root, shipping
from .project import find_project, read_project
from .shell import program
from .toolchain import build_tools


def cmd_doctor(args) -> int:
    """Check the engine, the toolchain and the project, with a fix for each problem."""
    problems = 0

    def report(state: str, what: str, found: str, fix: str = ""):
        nonlocal problems
        if state == "!!":
            problems += 1
        print(f"  [{state}] {what:<13} {found}")
        if fix:
            print(f"       {'':<13} -> {fix}")

    root = sdk_root()
    dev = development(root)
    ship = shipping(root)
    version = dev.version()
    print("engine")
    if version:
        layout = "the engine build tree" if in_engine_tree(root) else "an installed SDK"
        report("ok", "version", f"{version}, {layout} at {root}")
    else:
        report("!!", "version", f"none readable under {root}", "build the engine, or reinstall the SDK")
    missing = [n for n in ("vkm_runtime", "vkm_editor", "vkm_cook", "vkm_server") if not dev.find_host(n)]
    if missing:
        report("!!", "hosts", "missing " + ", ".join(missing), "build the engine, or reinstall the SDK")
    else:
        report("ok", "hosts", "runtime, editor, cooker and server")
    if dev.profiler():
        report("ok", "profiler", "Tracy built in, listening on this machine only")
    report("ok", "asserts", "on" if dev.asserts() else "off in this build (VKM_ASSERTS)")
    if ship.exists():
        report("ok", "shipping", f"built, at {ship.base}; packages ship on it")
    elif in_engine_tree(root):
        report("--", "shipping", "not built yet", "`vkm package` builds it the first time")
    else:
        report("--", "shipping", "this SDK carries none, so packages ship on the development engine")

    print("toolchain")
    for tool in build_tools(dev):
        if tool.present():
            report("ok", tool.name, f"{tool.version} at {tool.dir}")
        else:
            report("--", tool.name, f"{tool.version}, not fetched yet", "`vkm build` fetches it")
    if dev.pinned():
        # The pinned GCC compiles against the system's C library, as every native compiler does.
        if os.name != "nt" and not Path("/usr/include/stdio.h").is_file():
            report(
                "!!", "C headers", "the system's C library headers are missing",
                "install them: `sudo apt install libc6-dev` (Debian, Ubuntu), `glibc-devel` (Fedora)"
            )
    else:
        compiler = dev.compiler()
        if not compiler:
            report("--", "compiler", "the build records none; CMake picks one")
        elif Path(compiler).exists() or shutil.which(compiler):
            report("ok", "compiler", f"{compiler}, the one the engine was built with")
        else:
            report(
                "!!",
                "compiler",
                f"the engine was built with {compiler}, which is not here",
                "install it, or `vkm build --compiler <an equivalent>`"
            )
        objcopy = program("objcopy")
        report("ok" if objcopy else "--", "objcopy", objcopy or "not on PATH; a package keeps its debug info")
    report("ok", "python", f"{platform.python_version()} at {sys.executable}")

    d = find_project(args.project)
    if not d:
        print("project")
        report("--", "project", "none here; name one, or make one with `vkm new <name>`")
        return 1 if problems else 0

    meta = read_project(d)
    print(f"project {meta.get('name') or d.name}")
    report("ok", "directory", str(d))

    made_with = meta.get("engineVersion", "")
    if not version or made_with == version:
        report("ok", "made for", made_with or "no engine named")
    else:
        report(
            "!!", "made for", f"engine {made_with}; this one is {version}",
            "move it: set engineVersion in project.json"
        )

    module = d / "bin" / MODULE
    sources = [f for f in (d / "src").rglob("*") if f.is_file()] if (d / "src").is_dir() else []
    builds_first = "`vkm build`; run, serve and package build it first"
    if not module.is_file():
        report("--", "module", f"bin/{MODULE} not built yet", builds_first)
    elif any(f.stat().st_mtime > module.stat().st_mtime for f in sources):
        report("--", "module", f"bin/{MODULE} is older than src/", builds_first)
    else:
        report("ok", "module", f"bin/{MODULE}, newer than src/")

    entry = meta.get("entryScene", "")
    if not entry:
        report("ok", "entry scene", "none; the module builds the world")
    elif (d / entry).is_file():
        report("ok", "entry scene", entry)
    else:
        report("!!", "entry scene", f"{entry} does not exist", "set entryScene, or save that scene")

    if (d / "cooked" / "_manifest.json").is_file():
        report("ok", "cooked", "cooked/ holds a manifest")
    else:
        report("--", "cooked", "not cooked yet", "`vkm cook`; run, serve and package cook first")

    print()
    print(f"{problems} problem(s)" if problems else "nothing wrong")
    return 1 if problems else 0
