"""Assembling the game a player gets."""

from __future__ import annotations

import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

from .build import build_module, build_shipping_engine, cook, module_dir
from .engine import EngineBuild, MODULE, development, in_engine_tree, sdk_root, shipping
from .project import project_dir, project_name, read_project
from .shell import EXE, capture, die, megabytes, platform_tag, program, say, size_of, step


def borrowed_libraries(exe: Path) -> list[str]:
    """Libraries the package loads from outside itself, so it would run only on this machine.

    The system's own libraries are the player's; empty where no check can run.
    """
    if sys.platform.startswith("linux"):
        return borrowed_on_linux(exe)
    if os.name == "nt":
        return borrowed_on_windows(exe.parent)
    return []


def borrowed_on_linux(exe: Path) -> list[str]:
    """What the dynamic loader resolves outside the package, as ldd reports it."""
    try:
        out = subprocess.run(["ldd", str(exe)], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return []

    here = str(exe.parent.resolve())
    bad = []
    for line in out.splitlines():
        if "=>" not in line:
            continue
        name, _, rest = line.partition("=>")
        # "name => /path/to/lib (0x...)", where the path may hold a space.
        path = re.sub(r"\s+\(0x[0-9a-fA-F]+\)$", "", rest.strip())
        if not path.startswith("/"):
            continue
        if path.startswith(("/lib", "/usr/lib", "/lib64", "/usr/lib64")):
            continue
        if not path.startswith(here):
            bad.append(f"{name.strip()} -> {path}")
    return bad


def borrowed_on_windows(bindir: Path) -> list[str]:
    """Each DLL a binary in the package imports that is neither in it nor Windows'.

    Read with objdump or llvm-objdump; MSYS2's ldd would start each binary instead.
    """
    objdump = program("objdump") or program("llvm-objdump")
    if not objdump:
        return []
    system = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
    carried = {f.name.lower() for f in bindir.iterdir()}

    bad = []
    for binary in sorted(bindir.iterdir()):
        if binary.suffix.lower() not in (".exe", ".dll"):
            continue
        rc, out = capture([objdump, "-p", binary])
        for dll in re.findall(r"DLL Name:\s*(\S+)", out) if rc == 0 else []:
            # API sets, which Windows resolves itself.
            if dll.lower() in carried or dll.lower().startswith(("api-ms-", "ext-ms-")):
                continue
            if not (system / dll).exists():
                bad.append(f"{binary.name} -> {dll}")
    return bad


def split_debug_info(bindir: Path, symbols: Path) -> bool:
    """Move each binary's debug info into `symbols`, linked from the binary; False without objcopy.

    It is most of the engine's size, and kept to read a player's crash with.
    """
    objcopy = program("objcopy")
    if not objcopy:
        return False
    if symbols.exists():
        shutil.rmtree(symbols)
    symbols.mkdir(parents=True)

    for f in sorted(bindir.iterdir()):
        # A versioned library's links lead to one file, which is the one split.
        if f.is_symlink() or not f.is_file():
            continue
        with f.open("rb") as stream:
            magic = stream.read(4)
        if magic != b"\x7fELF" and magic[:2] != b"MZ":
            continue
        debug = symbols / (f.name + ".debug")
        for cmd in (
            [objcopy, "--only-keep-debug", f, debug],
            [objcopy, "--strip-debug", f"--add-gnu-debuglink={debug}", f],
        ):
            rc, out = capture(cmd)
            if rc:
                die(f"objcopy could not split the debug info out of {f.name}:\n{out}")
    return True


# The game's icon, where the runtime finds its window's (app/engine_app.h).
PROJECT_ICON = Path("assets") / "logo" / "icon.png"


def png_size(data: bytes) -> tuple[int, int] | None:
    """A PNG's width and height from its header, or None for anything else."""
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", data[16:24])


def group_icon(png: bytes, width: int, height: int) -> bytes:
    """An RT_GROUP_ICON naming one PNG image, resource 1; an edge of 256 is written 0."""
    header = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack("<BBBBHHIH", width % 256, height % 256, 0, 0, 1, 32, len(png), 1)
    return header + entry


def version_resource(name: str, version: str, filename: str) -> bytes:
    """An RT_VERSION block naming the game, as Properties > Details shows it.

    A VS_VERSIONINFO tree: each node its lengths, type, key, value and children,
    each padded to four bytes.
    """
    def pad(data: bytes) -> bytes:
        return data + b"\0" * (-len(data) % 4)

    def node(key: str, value: bytes = b"", value_length: int = 0, text: bool = False, children=()) -> bytes:
        body = pad(struct.pack("<HHH", 0, value_length, 1 if text else 0) + (key + "\0").encode("utf-16-le"))
        body = pad(body + value) if value else body
        for child in children:
            body = pad(body) + child
        return struct.pack("<H", len(body)) + body[2:]

    def string(key: str, text: str) -> bytes:
        return node(key, (text + "\0").encode("utf-16-le"), len(text) + 1, text=True)

    numbers = [int(n) if n.isdigit() else 0 for n in re.split(r"[.\-+]", version)[:4]]
    numbers += [0] * (4 - len(numbers))
    high, low = (numbers[0] << 16) | numbers[1], (numbers[2] << 16) | numbers[3]
    # Signature, struct version, file and product version, flags, NT, an application, no date.
    fixed = struct.pack("<13I", 0xFEEF04BD, 0x00010000, high, low, high, low, 0x3F, 0, 0x40004, 1, 0, 0, 0)

    table = node("040904B0", text=True, children=[
        string("FileDescription", name),
        string("FileVersion", version),
        string("InternalName", name),
        string("OriginalFilename", filename),
        string("ProductName", name),
        string("ProductVersion", version),
    ])
    translation = node("Translation", struct.pack("<HH", 0x0409, 0x04B0), 4)
    strings = node("StringFileInfo", text=True, children=[table])
    variables = node("VarFileInfo", text=True, children=[translation])
    return node("VS_VERSION_INFO", fixed, len(fixed), children=[strings, variables])


def stamp_windows_exe(exe: Path, name: str, version: str, icon: Path) -> str | None:
    """Write the game's name, version and icon into a packaged executable's resources.

    Answers why it could not, or None; a package is whole without them.
    """
    import ctypes
    from ctypes import wintypes

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.BeginUpdateResourceW.restype = wintypes.HANDLE
    kernel.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    kernel.UpdateResourceW.restype = wintypes.BOOL
    kernel.UpdateResourceW.argtypes = [
        wintypes.HANDLE,
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.WORD,
        ctypes.c_char_p,
        wintypes.DWORD
    ]
    kernel.EndUpdateResourceW.restype = wintypes.BOOL
    kernel.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]

    RT_ICON, RT_GROUP_ICON, RT_VERSION = 3, 14, 16
    US_ENGLISH, NEUTRAL = 0x0409, 0
    blocks = [(RT_VERSION, US_ENGLISH, version_resource(name, version or "0", exe.name))]
    note = None
    if icon.is_file():
        png = icon.read_bytes()
        size = png_size(png)
        if size and max(size) <= 256:
            blocks += [(RT_ICON, NEUTRAL, png), (RT_GROUP_ICON, NEUTRAL, group_icon(png, *size))]
        else:
            note = f"{icon.name} is not a PNG of at most 256x256, so the executable keeps the engine's icon"

    handle = kernel.BeginUpdateResourceW(str(exe), False)
    if not handle:
        return f"its resources could not be opened (error {ctypes.get_last_error()})"
    for kind, language, data in blocks:
        if not kernel.UpdateResourceW(handle, kind, 1, language, data, len(data)):
            error = ctypes.get_last_error()
            kernel.EndUpdateResourceW(handle, True)
            return f"its resources could not be written (error {error})"
    if not kernel.EndUpdateResourceW(handle, False):
        return f"its resources could not be saved (error {ctypes.get_last_error()})"
    return note


# Engine data a package carries, as cmake/install.cmake installs it; the repo's
# assets/ also holds sample art no game ships. shaders/ is also how
# io/project_paths.cpp recognises a packaged layout.
ENGINE_DATA = ("shaders", "assets/fonts", "assets/logo")


# The logo's design sources, which nothing loads; cmake/install.cmake leaves them out too.
LOGO_SOURCES = frozenset({"_preview", "source_split"})


# Project directories a game reads while it plays.
PROJECT_DATA = ("scenes", "prefabs", "cooked")


# The recipes a game reads: a material's is its runtime form, every other kind
# cooks to a binary.
RUNTIME_LIBRARY = ("materials",)


def import_sources(project: Path) -> set[Path]:
    """Every source file the cook imported from, as the manifest records it.

    A package leaves these out; anything else in assets/ ships, since the runtime
    opens some files by path. An unreadable manifest leaves nothing out.
    """
    try:
        manifest = json.loads((project / "cooked" / "_manifest.json").read_text())
    except (OSError, ValueError):
        return set()
    rows = manifest.get("assets") if isinstance(manifest, dict) else None
    sources: set[Path] = set()
    for row in rows if isinstance(rows, list) else []:
        files = row.get("sources") if isinstance(row, dict) else None
        for path in files if isinstance(files, list) else []:
            if isinstance(path, str) and path:
                sources.add((project / path).resolve())
    return sources


def copy_except(src: Path, dst: Path, skip: set[Path]):
    """Copy a tree less the files and folders in `skip`, pruning folders left empty."""
    shutil.copytree(
        src,
        dst,
        dirs_exist_ok=True,
        ignore=lambda directory, names: {n for n in names if (Path(directory) / n).resolve() in skip}
    )

    for dirpath, _, _ in os.walk(dst, topdown=False):
        empty = Path(dirpath)
        if not any(empty.iterdir()):
            empty.rmdir()


def shipping_engine_for(root: Path, args) -> EngineBuild:
    """The engine a package is built on: the shipping one, unless --development or the SDK has none."""
    if args.development:
        return development(root)
    if in_engine_tree(root):
        return build_shipping_engine(root, args.jobs)
    engine = shipping(root)
    if engine.exists():
        return engine
    say("this SDK carries no shipping engine, so the game ships on the development one, profiler and all")
    return development(root)


def cmd_package(args) -> int:
    """Assemble a standalone game: the runtime renamed, its libraries, the module and the data it plays.

    Source art and import recipes stay behind: the runtime reads only what was cooked.
    """
    start = time.monotonic()
    d = project_dir(args.project)
    root = sdk_root()
    meta = read_project(d)
    name = args.name or project_name(d)
    version = meta.get("version") or ""

    tag = "-".join(part for part in (name, version, platform_tag()) if part)
    out = Path(args.out).resolve() if args.out else d / "dist" / tag
    symbols = out.parent / (out.name + "-symbols")

    # Both are emptied below, so neither may hold anything else (`-o ~/Desktop`).
    if out.is_dir() and any(out.iterdir()) and not (out / "project.json").is_file():
        die(f"'{out}' holds something other than a package; name a new or empty folder with -o")
    if symbols.is_dir() and any(f.is_file() and f.suffix != ".debug" for f in symbols.rglob("*")):
        die(f"'{symbols}' holds something other than debug info, and a package would empty it; move it first")

    engine = shipping_engine_for(root, args)
    if rc := cook(d) or build_module(d, engine, jobs=args.jobs):
        return rc

    built = module_dir(d, engine)
    if not (built / MODULE).is_file():
        die(f"the build left no module at {built / MODULE}, and the runtime plays nothing without one")

    say(f"packaging {name}")
    with step("clearing the last package"):
        if out.exists():
            shutil.rmtree(out)
        (out / "bin").mkdir(parents=True)

    runtime = engine.host("vkm_runtime")
    game_exe = out / "bin" / (name + EXE)
    with step("the runtime, as the game", game_exe):
        shutil.copy2(runtime, game_exe)

    # A multiplayer game is hosted by the dedicated server alone (docs/reference/networking.md).
    if args.server:
        with step("the server, as the game's", out / "bin" / (name + "-server" + EXE)):
            shutil.copy2(engine.host("vkm_server"), out / "bin" / (name + "-server" + EXE))

    # Every library the engine keeps, beside the executable; a versioned chain
    # (libglfw.so -> libglfw.so.3) arrives as links, which the loader follows.
    copied = 0
    with step("engine libraries"):
        for libdir in engine.lib_dirs:
            for pattern in ("*.so*", "*.dll"):
                for lib in libdir.glob(pattern):
                    shutil.copy2(lib, out / "bin" / lib.name, follow_symlinks=False)
                    copied += 1

    if not copied:
        die(f"no engine libraries beside {runtime}; a package without them would run only on this machine")

    # Not the ".loaded." copies a running host makes, nor an import library.
    with step("the gameplay module"):
        for f in built.iterdir():
            if f.is_file() and ".loaded." not in f.name and f.suffix not in (".a", ".lib"):
                shutil.copy2(f, out / "bin" / f.name)

    for part in ENGINE_DATA:
        src = root / part
        if src.is_dir():
            with step(f"engine {part}", out / part):
                shutil.copytree(
                    src,
                    out / part,
                    dirs_exist_ok=True,
                    ignore=lambda _dir, names: [n for n in names if n in LOGO_SOURCES]
                )

    # The game ships the engine's libraries, so it ships the engine's license.
    if not (root / "LICENSE").is_file():
        die(f"no LICENSE in {root}; its terms go with every copy of the engine's libraries")
    with step("the engine's license", out / "LICENSE-vkmEngine.txt"):
        shutil.copy2(root / "LICENSE", out / "LICENSE-vkmEngine.txt")

    # At the package root, which ProjectPaths::projectRoot falls back to.
    shutil.copy2(d / "project.json", out / "project.json")
    for part in PROJECT_DATA:
        if (d / part).is_dir():
            with step(f"project {part}", out / part):
                shutil.copytree(d / part, out / part, dirs_exist_ok=True)

    for part in RUNTIME_LIBRARY:
        src = d / "library" / part
        if src.is_dir():
            with step(f"project library/{part}", out / "library" / part):
                shutil.copytree(src, out / "library" / part, dirs_exist_ok=True)

    if (d / "assets").is_dir():
        with step("project assets, less what the cook imported", out / "assets"):
            copy_except(d / "assets", out / "assets", import_sources(d))

    split = False
    if not args.debug_info:
        with step("debug info, moved out beside the package", symbols):
            split = split_debug_info(out / "bin", symbols)
        if not split:
            say("no objcopy on PATH, so the package keeps its debug info")

    # After the split, which reads the executable as the linker wrote it.
    if os.name == "nt":
        with step("its icon and version, for Explorer"):
            note = stamp_windows_exe(game_exe, name, version, d / PROJECT_ICON)
        if note:
            say(f"{game_exe.name}: {note}")

    with step("checking it carries every library it loads"):
        borrowed = borrowed_libraries(game_exe)
    if borrowed:
        die(
            "the package borrows libraries it does not carry, so it would run "
            "only on this machine:\n  " + "\n  ".join(borrowed)
        )

    archive = None
    if args.archive:
        # A tarball off Windows keeps the library links and executable bits.
        kind = "zip" if os.name == "nt" else "gztar"
        with step("archiving"):
            archive = Path(shutil.make_archive(str(out), kind, root_dir=out.parent, base_dir=out.name))

    # Quoted for a POSIX shell, so "Physics Lab" pastes.
    quote = (lambda p: str(p)) if os.name == "nt" else (lambda p: shlex.quote(str(p)))
    print()
    say(f"packaged {name} in {time.monotonic() - start:.1f} s")
    print(f"  the game     {quote(out)}  ({megabytes(size_of(out))})")
    if split:
        print(f"  its symbols  {quote(symbols)}  (keep them to read a crash; do not ship them)")
    if archive:
        print(f"  archived as  {quote(archive)}  ({megabytes(archive.stat().st_size)})")
    print(f"  play it      {quote(game_exe)}")
    if not engine.shipping:
        print("  engine       the development one, profiler and all - for testing, not for players")
    return 0
