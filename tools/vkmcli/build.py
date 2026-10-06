"""Compiling a project's module, and cooking its assets."""

from __future__ import annotations

import re
import shutil
import subprocess
import time
from pathlib import Path

from . import shell
from .engine import EngineBuild, development, shipping
from .project import project_dir, project_name
from .shell import capture, die, program, run, say
from .toolchain import build_tools, module_compiler, tools_on_path


def cmake_build(
    label: str,
    source: Path,
    build: Path,
    configure: list,
    compiler: str | None = None,
    generator: str | None = None,
    targets=(),
    jobs: int | None = None,
    reconfigure: bool = False
) -> int:
    """Configure a CMake build tree once, then build it incrementally.

    `compiler` and `generator` apply to a first configure: CMake cannot change them.
    """
    cmake = program("cmake") or die("cmake is not on PATH. Install CMake 3.25 or newer.")

    # A configure that failed keeps failing on its cached choices; start it over.
    generated = ("build.ninja", "Makefile", "CMakeFiles/Makefile.cmake")
    configured = any((build / g).exists() for g in generated) or any(build.glob("*.sln"))
    if (build / "CMakeCache.txt").exists() and not configured:
        say(f"{build} holds a configure that never finished; starting it over")
        shutil.rmtree(build)

    if not configured or reconfigure:
        cfg = [cmake, "-S", source, "-B", build, *configure]
        first = not (build / "CMakeCache.txt").exists()
        if compiler and first:
            # Said here, rather than letting CMake fall back to another compiler.
            if not Path(compiler).exists() and not shutil.which(compiler):
                die(
                    f"the engine was built with '{compiler}', which is not on this "
                    f"machine.\n     Install it, or name an equivalent with "
                    f"`vkm build --compiler`, or rebuild the engine here."
                )
            cfg.append(f"-DCMAKE_CXX_COMPILER={compiler}")
        # Ninja when there is one: the fastest at the no-op build every run starts with.
        generator = generator or ("Ninja" if program("ninja") else None)
        if generator and first:
            cfg += ["-G", generator]

        # CMake's output is shown only when it fails.
        say(f"configuring {label}")
        rc, out = capture(cfg)
        if rc or shell.VERBOSE:
            print(out, end="")
        if rc:
            say(f"configuring {label} failed; the output above says why")
            return rc

    say(f"building {label}")
    step = [cmake, "--build", build]
    if targets:
        step += ["--target", *targets]
    if jobs:
        step += ["--parallel", str(jobs)]
    return run(step)


def module_dir(d: Path, engine: EngineBuild) -> Path:
    """Where a project's module built against `engine` lands.

    A shipping one has its own, so it never replaces the module bin/ holds for the
    development hosts (VKM_MODULE_DIR, cmake/gameplay_module.cmake).
    """
    return d / "build" / "shipping" / "bin" if engine.shipping else d / "bin"


def build_module(
    d: Path,
    engine: EngineBuild,
    jobs: int | None = None,
    compiler: str | None = None,
    generator: str | None = None,
    cmake_args=()
) -> int:
    """Compile a project's src/ into its module against `engine`, in build/development/ or build/shipping/."""
    if not (d / "CMakeLists.txt").is_file():
        say(f"{project_name(d)} has no CMakeLists.txt, so there is no module to build")
        return 0
    if not engine.exists():
        die(
            f"no vkmEngine package in {engine.package}. Build the engine (which writes "
            f"one into its build tree), or install the SDK with `cmake --install`."
        )

    configure = [f"-DvkmEngine_DIR={engine.package}", f"-DCMAKE_PREFIX_PATH={engine.base}"]
    if engine.shipping:
        configure.append(f"-DVKM_MODULE_DIR={module_dir(d, engine)}")
    # The engine's configuration, unless the caller names one; unset means no optimisation.
    build_type = engine.build_type()
    if build_type and not any(a.startswith("-DCMAKE_BUILD_TYPE") for a in cmake_args):
        configure.append(f"-DCMAKE_BUILD_TYPE={build_type}")
    configure += list(cmake_args)

    tools_on_path(build_tools(engine))
    return cmake_build(
        f"{project_name(d)}{' for shipping' if engine.shipping else ''}",
        d,
        d / "build" / ("shipping" if engine.shipping else "development"),
        configure,
        compiler=compiler or module_compiler(engine),
        generator=generator,
        jobs=jobs,
        reconfigure=bool(cmake_args)
    )


def build_shipping_engine(root: Path, jobs: int | None) -> EngineBuild:
    """Build the engine tree's shipping engine (VKM_SHIPPING) in build-shipping/, or bring it up to date."""
    engine = shipping(root)
    if not engine.exists():
        say("the shipping engine is built once, which takes a while; later packages build only what changed")
    # A pinned development engine names no compiler, so this one takes the pinned GCC too.
    dev = development(root)
    tools_on_path(build_tools(dev))
    rc = cmake_build(
        "the shipping engine",
        root,
        engine.base,
        ["-DVKM_SHIPPING=ON"],
        compiler=None if dev.pinned() else dev.compiler(),
        targets=("vkm_runtime_app", "vkm_server_app"),
        jobs=jobs
    )
    if rc:
        die("the shipping engine did not build; the output above says why")
    return engine


# A log line from a host: "[time] [TAG] [CATEGORY] [LEVEL] message".
LOG_LINE = re.compile(r"^\[[^\]]*\] \[[^\]]*\] \[(\w+)\] \[(\w+)\] (.*)$")


def cook(d: Path) -> int:
    """Cook a project, showing the cook's own steps and warnings; the rest only if it fails."""
    exe = development().host("vkm_cook")
    if shell.VERBOSE:
        return run([exe, d])

    say(f"cooking {project_name(d)}")
    start = time.monotonic()
    held = []
    proc = subprocess.Popen(
        [str(exe), str(d)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        encoding="utf-8",
        errors="replace"
    )
    for line in proc.stdout:
        held.append(line)
        m = LOG_LINE.match(line.rstrip("\n"))
        if m and (m.group(1) == "COOK" or m.group(2) in ("WARNING", "ERROR", "FATAL")):
            level = "" if m.group(2) == "INFO" else f"{m.group(2).lower()}: "
            print(f"  {level}{m.group(3)}", flush=True)
    rc = proc.wait()
    if rc:
        print("".join(held), end="")
        say(f"cooking {project_name(d)} failed (code {rc}); its whole log is above")
        return rc
    say(f"cooked in {time.monotonic() - start:.1f} s")
    return 0


def up_to_date(d: Path, jobs: int | None) -> int:
    """Build and cook a project, so what starts next is what is on disk."""
    return build_module(d, development(), jobs=jobs) or cook(d)


def cmd_build(args) -> int:
    return build_module(
        project_dir(args.project),
        development(),
        jobs=args.jobs,
        compiler=args.compiler,
        generator=args.generator,
        cmake_args=args.cmake_args
    )


def cmd_cook(args) -> int:
    return cook(project_dir(args.project))
