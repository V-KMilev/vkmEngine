"""The engine vkm drives: where it is, and the builds of it a project runs on."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from .shell import EXE, die


# The folder vkm.py is in: tools/ in the engine's tree, bin/ in an SDK.
TOOL_DIR = Path(__file__).resolve().parent.parent


def sdk_root() -> Path:
    """The engine root: the directory above vkm.py's, or VKM_ENGINE_ROOT."""
    if env := os.environ.get("VKM_ENGINE_ROOT"):
        return Path(env).resolve()

    if (TOOL_DIR.parent / "shaders").is_dir():
        return TOOL_DIR.parent
    die("cannot locate the engine. Set VKM_ENGINE_ROOT to the SDK directory.")


def in_engine_tree(root: Path) -> bool:
    """Whether `root` is the engine's source tree rather than an installed SDK."""
    return (root / "CMakeLists.txt").is_file() and (root / "src" / "engine").is_dir()


# The module every host loads (vkm_add_gameplay_module, cmake/gameplay_module.cmake).
MODULE = "game.dll" if os.name == "nt" else "libgame.so"


class EngineBuild:
    """One build of the engine: its hosts and libraries, and the package a module builds against.

    Installed or not, a build has one shape: bin/, lib/ and lib/cmake/vkmEngine.
    """

    def __init__(self, base: Path, shipping: bool):
        self.base = base
        self.shipping = shipping
        self.bin = base / "bin"
        self.lib_dirs = [self.bin, base / "lib"]
        self.package = base / "lib" / "cmake" / "vkmEngine"

    def exists(self) -> bool:
        return (self.package / "vkmEngineConfig.cmake").is_file()

    def setting(self, filename: str, pattern: str) -> str | None:
        """The first capture of `pattern` in a file of this build's package."""
        path = self.package / filename
        if not path.is_file():
            return None
        m = re.search(pattern, path.read_text())
        return m.group(1) if m else None

    def compiler(self) -> str | None:
        """The C++ compiler this build was made with; a module has to use the same one."""
        return self.setting("vkmEngineConfig.cmake", r'set\(VKMENGINE_BUILD_CXX_COMPILER\s+"([^"]+)"\)')

    def build_type(self) -> str | None:
        """The configuration this build was made as; None for a multi-config generator."""
        return self.setting("vkmEngineConfig.cmake", r'set\(VKMENGINE_BUILD_TYPE\s+"([^"]+)"\)')

    def version(self) -> str | None:
        """The engine's full version, which a new project's project.json records."""
        return self.setting("vkmEngineConfigVersion.cmake", r'set\(PACKAGE_VERSION\s+"(\d+\.\d+\.\d+)"')

    def profiler(self) -> bool:
        """Whether Tracy is compiled into this build."""
        return self.switch("VKMENGINE_PROFILER")

    def asserts(self) -> bool:
        """Whether VKM_ASSERT and assert() run in this build."""
        return self.switch("VKMENGINE_ASSERTS")

    def switch(self, name: str) -> bool:
        """A boolean the package's config records."""
        setting = self.setting("vkmEngineConfig.cmake", r'set\(' + name + r'\s+"([^"]*)"\)')
        return (setting or "").upper() in ("ON", "1", "TRUE", "YES")

    def find_host(self, name: str) -> Path | None:
        found = self.bin / (name + EXE)
        return found if found.exists() else None

    def host(self, name: str) -> Path:
        found = self.find_host(name)
        if not found:
            die(f"{name} not found in {self.bin}. Build the engine first, or check the SDK.")
        return found


def development(root: Path | None = None) -> EngineBuild:
    """The engine every command but `package` runs: the tree's build/, or the SDK itself."""
    root = root or sdk_root()
    return EngineBuild(root / "build" if in_engine_tree(root) else root, False)


def shipping(root: Path | None = None) -> EngineBuild:
    """The engine a package ships on: the tree's build-shipping/, or an SDK's shipping/."""
    root = root or sdk_root()
    return EngineBuild(root / "build-shipping" if in_engine_tree(root) else root / "shipping", True)


def toolchain_first(compiler: str | None):
    """Put the engine's toolchain first on PATH, on Windows.

    Windows finds DLLs through PATH, and MSYS2's environments share DLL names: a
    UCRT64 compiler started from a MINGW64 shell loads the wrong ones and fails
    silently, and so do the engine's executables.
    """
    if os.name != "nt" or not compiler:
        return
    found = Path(compiler) if Path(compiler).exists() else Path(shutil.which(compiler) or compiler)
    bindir = str(found.parent.resolve())
    path = os.environ.get("PATH", "").split(os.pathsep)
    rest = [p for p in path if p and Path(p).resolve() != Path(bindir)]
    os.environ["PATH"] = os.pathsep.join([bindir] + rest)


def temp_dir_set():
    """Set TMP and TEMP on Windows when they name no directory.

    Without them GCC falls back to the Windows directory and every compile fails.
    """
    if os.name != "nt":
        return
    fallback = Path(os.environ.get("LOCALAPPDATA", "")) / "Temp"
    usable = str(fallback) if os.environ.get("LOCALAPPDATA") and fallback.is_dir() else tempfile.gettempdir()
    for var in ("TMP", "TEMP"):
        if not os.path.isdir(os.environ.get(var, "")):
            os.environ[var] = usable


def native_python_on_windows():
    """Restart under a Windows Python when MSYS2's POSIX one started this tool.

    MSYS2's Python reports a POSIX system and spells paths /d/..., so the Windows
    half of this tool would not run.
    """
    if sys.platform not in ("cygwin", "msys"):
        return

    def cygpath(flag: str, p: str) -> str:
        """`p` in the other system's spelling, or as it was if cygpath cannot say."""
        try:
            done = subprocess.run(["cygpath", flag, p], stdout=subprocess.PIPE, universal_newlines=True)
        except OSError:
            return p
        return done.stdout.strip() if done.returncode == 0 and done.stdout.strip() else p

    # The Python the installer puts in an SDK, then one beside the compiler that built the engine.
    candidates = [[str(TOOL_DIR.parent / "python" / "python.exe")]]
    if compiler := development().compiler():
        bindir = Path(cygpath("-u", compiler)).parent
        candidates += [[str(bindir / "python3.exe")], [str(bindir / "python.exe")]]
    if py := shutil.which("py"):
        candidates.append([py, "-3"])

    # A Windows program cannot read /d/...; a relative path reads the same either way.
    argv = [cygpath("-w", a) if a.startswith("/") and Path(a).exists() else a for a in sys.argv[1:]]
    for interpreter in candidates:
        if Path(interpreter[0]).is_file():
            os.execv(interpreter[0], [*interpreter, cygpath("-w", str(TOOL_DIR / "vkm.py")), *argv])

    die(
        f"this is MSYS's own Python ({sys.executable}), which cannot drive Windows programs.\n"
        f"     Install a Windows one beside the engine's compiler - in an MSYS2 shell,\n"
        f"     `pacman -S mingw-w64-ucrt-x86_64-python` - or from python.org, and vkm uses it."
    )
