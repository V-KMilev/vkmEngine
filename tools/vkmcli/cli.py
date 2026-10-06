"""The command line: the help, the parser, and main()."""

from __future__ import annotations

import argparse
import sys

from . import shell
from .build import cmd_build, cmd_cook
from .doctor import cmd_doctor
from .engine import development, native_python_on_windows, sdk_root, temp_dir_set, toolchain_first
from .package import cmd_package
from .play import cmd_edit, cmd_run, cmd_serve
from .project import cmd_clean, cmd_new
from .shell import die
from .toolchain import BUILD_TOOLS, cmd_toolchain, uses_pinned_tools


HELP = """\
usage: vkm <command> [project] [options]

The project tool for vkmEngine: it builds, runs and ships a game.

A project is a directory holding a project.json. Every command but `new` takes
its path, or finds it from the current directory - from any folder inside it.

Make
  new <name>    make a project from a template or an example (-t physics_lab)

Work on it
  run           build, cook and play it            --players N: with a local server
  edit          build it and open it in the editor
  serve         build, cook and host it for players, with no window
  build         only compile src/ into the gameplay module
  cook          only turn source art into what the runtime reads

Ship
  package       build, cook and assemble the game a player gets, under dist/

Look after
  doctor        check the engine, the toolchain and the project, and say what to fix
  toolchain     fetch the compiler and build tools the engine pins (build does it first)
  clean         delete everything the commands above generated

`vkm <command> -h` explains one, with examples. `vkm --version` names the engine.
"""


def subcommand(sub, parents, name: str, summary: str, examples: str, fn, project=True):
    """One command's parser, with its examples under its options."""
    p = sub.add_parser(
        name,
        parents=parents,
        help=summary,
        description=summary[0].upper() + summary[1:] + ".",
        epilog="examples:\n" + examples,
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    if project:
        p.add_argument("project", nargs="?", help="the project's folder, or any folder in it (default: here)")
    p.set_defaults(fn=fn)
    return p


def main() -> int:
    native_python_on_windows()
    argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help"):
        print(HELP, end="")
        return 0
    if argv[0] == "help":
        argv = argv[1:2] + ["-h"] if len(argv) > 1 else ["-h"]
        if argv == ["-h"]:
            print(HELP, end="")
            return 0
    if argv[0] in ("-V", "--version"):
        root = sdk_root()
        print(f"vkm for vkmEngine {development(root).version() or '(unknown version)'} at {root}")
        return 0

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("-v", "--verbose", action="store_true", help="show each command and all its output")
    builds = argparse.ArgumentParser(add_help=False)
    builds.add_argument("-j", "--jobs", type=int, help="compile at most this many files at once")

    ap = argparse.ArgumentParser(prog="vkm", usage="vkm <command> [project] [options]", add_help=False)
    sub = ap.add_subparsers(dest="cmd", metavar="<command>", required=True, prog="vkm")

    p = subcommand(sub, [common], "new", "make a project from a template or an example", (
        "  vkm new mygame              a folder mygame/ here, then: cd mygame && vkm run\n"
        "  vkm new lab -t physics_lab  a copy of an example, yours to change\n"
    ), cmd_new, project=False)
    p.add_argument("name", help="the folder to make, whose name the game takes")
    p.add_argument(
        "--template",
        "-t",
        default="default",
        help="the template or example to copy (default: default)"
    )

    p = subcommand(sub, [common, builds], "run", "build, cook and play the project", (
        "  vkm run                     play the project you are in\n"
        "  vkm run examples/physics_lab\n"
        "  vkm run --players 2         a local server and two players, to test multiplayer\n"
        "  vkm run --connect 10.0.0.5  join a game someone is serving\n"
    ), cmd_run)
    p.add_argument("--players", "-n", type=int, metavar="N", help="serve it here and start N players on it")
    p.add_argument("--port", type=int, help="the port --players serves on (default: the project's netPort)")
    p.add_argument("--connect", metavar="HOST[:PORT]", help="join the game served there, not play alone")

    subcommand(sub, [common, builds], "edit", "build the project and open it in the editor", (
        "  vkm edit                    a failed build still opens it, on the module built last\n"
    ), cmd_edit)

    p = subcommand(sub, [common, builds], "serve", "build, cook and host the project, with no window", (
        "  vkm serve                   players join with: vkm run --connect <this machine>\n"
        "  vkm serve --port 28000\n"
    ), cmd_serve)
    p.add_argument("--port", type=int, help="port to listen on (default: the project's netPort)")

    p = subcommand(sub, [common, builds], "build", "compile the project's src/ into its gameplay module", (
        "  vkm build                   what run, edit, serve and package do first\n"
        "  vkm build -j 4              leave the machine some cores\n"
        "  vkm build -- -DMY_OPTION=ON pass arguments to CMake's configure\n"
    ), cmd_build)
    p.add_argument("-G", "--generator", help="CMake generator for a first configure (default: Ninja, if any)")
    p.add_argument("--compiler", help="C++ compiler for a first configure (default: the engine's own)")

    subcommand(sub, [common], "cook", "turn the project's source art into what the runtime reads", (
        "  vkm cook                    needs no window, so it runs on a build machine\n"
    ), cmd_cook)

    p = subcommand(sub, [common, builds], "package", "build, cook and assemble the game a player gets", (
        "  vkm package                 dist/<name>-<version>-<platform>/ on the shipping engine,\n"
        "                              and its symbols beside it\n"
        "  vkm package --archive       and a .zip (Windows) or .tar.gz of it, ready to hand out\n"
        "  vkm package --server        with the dedicated server, for a multiplayer game\n"
        "  vkm package --development   on the development engine: quick, for testing only\n"
    ), cmd_package)
    p.add_argument("-o", "--out", help="the folder to assemble it in (default: under dist/)")
    p.add_argument("--name", help="what to call the game's executable (default: the project's name)")
    p.add_argument("--server", action="store_true", help="also carry the dedicated server, as <name>-server")
    p.add_argument("--archive", action="store_true", help="also pack it into one file to hand out")
    p.add_argument(
        "--development",
        action="store_true",
        help="build it on the development engine: quicker, profiler in, for testing only"
    )
    p.add_argument(
        "--debug-info",
        action="store_true",
        help="keep the debug info in the binaries rather than beside them"
    )

    subcommand(sub, [common], "doctor", "check the engine, the toolchain and the project", (
        "  vkm doctor                  start a bug report with what this prints\n"
    ), cmd_doctor)

    p = subcommand(sub, [common], "toolchain", "fetch the compiler and build tools the engine pins", (
        "  vkm toolchain               fetch and verify GCC, CMake and Ninja; build does it the first time\n"
        "  vkm toolchain --path        print each one's bin/, to put on PATH\n"
        "  vkm toolchain python --dir  the pinned Python, which an SDK carries\n"
    ), cmd_toolchain, project=False)
    p.add_argument("tools", nargs="*", help=f"which tools (default: {', '.join(BUILD_TOOLS)})")
    where = p.add_mutually_exclusive_group()
    where.add_argument("--path", action="store_true", help="print only each tool's bin/, one per line")
    where.add_argument("--dir", action="store_true", help="print only each tool's folder, one per line")

    p = subcommand(sub, [common], "clean", "delete everything the other commands generated", (
        "  vkm clean                   build/ bin/ cooked/ logs/ - never your art, scenes or library/\n"
        "  vkm clean --all             and the packages under dist/\n"
    ), cmd_clean)
    p.add_argument("--all", action="store_true", help="delete dist/ too")

    # Split off by hand: argparse would take the first for the project.
    passed = argv[argv.index("--") + 1:] if "--" in argv else []
    if "--" in argv:
        argv = argv[:argv.index("--")]
    args = ap.parse_args(argv)
    if passed and args.cmd != "build":
        die(f"`vkm {args.cmd}` takes nothing after --; only `vkm build` passes arguments on, to CMake")
    args.cmake_args = passed
    shell.VERBOSE = args.verbose
    if args.cmd != "new":
        root = sdk_root()
        recorded = None if uses_pinned_tools(root) else development(root).compiler()
        toolchain_first(getattr(args, "compiler", None) or recorded)
        temp_dir_set()
    try:
        return args.fn(args)
    except KeyboardInterrupt:
        return 130
