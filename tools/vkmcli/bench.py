"""Timing the game a player gets against the budgets a project states."""

from __future__ import annotations

from argparse import Namespace

from .package import cmd_package
from .project import project_dir, project_name
from .shell import die, run


def cmd_bench(args) -> int:
    d = project_dir(args.project)
    plan = d / "bench.json"
    if not plan.is_file():
        die(f"no bench.json in {d}; docs/reference/building.md says what one holds")

    # The shipping engine, because a development one's asserts and profiler are not what a player runs.
    out = d / "build" / "shipping" / "package"
    package = Namespace(
        project=str(d),
        out=str(out),
        name=None,
        server=False,
        archive=False,
        development=False,
        debug_info=False,
        jobs=args.jobs
    )
    if rc := cmd_package(package):
        return rc
    return run([out / "bin" / project_name(d), "--bench", plan])
