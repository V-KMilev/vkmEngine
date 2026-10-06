"""A project on disk: finding it, making one, and cleaning what the tools wrote."""

from __future__ import annotations

import json
import shlex
import shutil
from pathlib import Path

from .engine import TOOL_DIR, development, sdk_root
from .shell import die, megabytes, say, size_of


def find_project(arg: str | None) -> Path | None:
    """The project at `arg` or the nearest directory above it, as findProjectRoot finds one."""
    probe = Path(arg or ".").resolve()
    while True:
        if (probe / "project.json").is_file():
            return probe
        if probe.parent == probe:
            return None
        probe = probe.parent


def project_dir(arg: str | None) -> Path:
    found = find_project(arg)
    if not found:
        die(
            f"no project.json in '{Path(arg or '.').resolve()}' or any directory above it.\n"
            f"     Name a project's directory, or make one with `vkm new <name>`."
        )
    return found


def read_project(d: Path) -> dict:
    try:
        return json.loads((d / "project.json").read_text())
    except json.JSONDecodeError as e:
        die(f"{d / 'project.json'} is not valid JSON: {e}")


def project_name(d: Path) -> str:
    return read_project(d).get("name") or d.name


# What the tools write into a project, in .gitignore's syntax: a new project's .gitignore,
# and, by name, what a copy of a project leaves out. install.cmake reads it too.
GENERATED_FILE = TOOL_DIR / "generated.txt"


def generated_names() -> frozenset[str]:
    """The names in GENERATED_FILE, which a copy skips at any depth."""
    lines = GENERATED_FILE.read_text().splitlines()
    return frozenset(line.strip().strip("/") for line in lines if line.strip() and not line.startswith("#"))


def templates(root: Path) -> dict[str, Path]:
    """What `vkm new` can copy, by folder name: the templates, then the examples."""
    found = {}
    for folder in ("templates", "examples"):
        if (root / folder).is_dir():
            for d in sorted((root / folder).iterdir()):
                if (d / "project.json").is_file():
                    found.setdefault(d.name, d)
    return found


def cmd_new(args) -> int:
    root = sdk_root()
    choices = templates(root)
    template = choices.get(args.template)
    if not template:
        known = ", ".join(choices) or "none"
        die(f"no template or example called '{args.template}' in {root} (there are: {known})")

    dest = Path(args.name).resolve()
    if dest.exists() and any(dest.iterdir()):
        die(f"'{dest}' already exists and is not empty")

    # Before anything is copied, so a broken SDK leaves no half-made project.
    version = development(root).version()
    if not version:
        die(f"cannot read the engine version from {root}; the SDK looks incomplete")

    generated = generated_names()
    shutil.copytree(
        template,
        dest,
        dirs_exist_ok=True,
        ignore=lambda _dir, names: [n for n in names if n in generated]
    )
    shutil.copyfile(GENERATED_FILE, dest / ".gitignore")

    # Keys sorted, as the engine writes them.
    pj = dest / "project.json"
    data = json.loads(pj.read_text())
    data["name"] = dest.name
    data["engineVersion"] = version
    pj.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")

    print(f"created {dest}")
    print(f"  cd {shlex.quote(args.name)} && vkm run")
    return 0


# What `vkm clean` deletes. Not library/: the recipes in it are source.
CLEANED = ("build", "bin", "cooked", "logs")


def cmd_clean(args) -> int:
    d = project_dir(args.project)
    removed = 0
    for part in CLEANED + (("dist",) if args.all else ()):
        target = d / part
        if not target.exists():
            continue
        size = size_of(target)
        try:
            shutil.rmtree(target)
        except OSError as e:
            die(f"could not delete {target}: {e}\n     Close what runs from the project, and try again.")
        print(f"  deleted {part}/ ({megabytes(size)})")
        removed += 1
    if not removed:
        say("nothing to clean")
    elif not args.all and (d / "dist").exists():
        say("dist/ is kept; `vkm clean --all` deletes the packages too")
    return 0
