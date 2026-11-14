"""A project from `vkm new` builds, cooks and packages into a game a player can be given, carrying what
it plays and nothing it was made from. How long that took is measured and printed, never failed on."""

import os
import shutil
import time
import unittest

import _vkm
from _vkm import vkm

from vkmcli import engine, toolchain  # noqa: E402 - _vkm put tools/ on the path
from vkmcli.shell import EXE  # noqa: E402

GAME = "game" + EXE  # the game's executable, as package names it


class NewProject(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.where = _vkm.scratch("new_project")
        cls.project = cls.where / "game"
        started = time.monotonic()
        cls.steps = [
            vkm("new", cls.project),
            vkm("build", cls.project, "-j", "4"),
            vkm("cook", cls.project),
            vkm("package", cls.project, "--development", "-j", "4"),
        ]
        took = time.monotonic() - started
        print(f"\nvkm new, build, cook and package: {took:.1f} s (the target: a game in under a minute)")
        packages = sorted((cls.project / "dist").glob("game-0.1.0-*")) if (cls.project / "dist").is_dir() else []
        cls.package = next((p for p in packages if not p.name.endswith("-symbols")), None)

    def test_each_step_succeeds(self):
        for ran in self.steps:
            self.assertEqual(ran.returncode, 0, ran.stdout)
        self.assertTrue((self.project / "bin" / engine.MODULE).is_file())
        self.assertTrue((self.project / "cooked" / "_manifest.json").is_file())

    def test_the_package_carries_the_game_its_engine_and_what_it_plays(self):
        self.assertIsNotNone(self.package, self.steps[-1].stdout)
        for part in (
            f"bin/{GAME}", f"bin/{engine.MODULE}", "shaders", "assets/fonts", "assets/logo",
            "project.json", "scenes/main.json", "cooked/_manifest.json", "licenses/vkmEngine.txt",
            "licenses/glfw.txt", "licenses/miniaudio.txt",
        ):
            self.assertTrue((self.package / part).exists(), part)
        self.assertTrue(any((self.package / "bin").glob("*vkm_core.*")), "the engine's library")
        if os.name != "nt":
            self.assertTrue(os.access(self.package / "bin" / GAME, os.X_OK))
        self.assertEqual(len(list((self.package / "cooked").rglob("*.vkmc"))), 2)
        self.assertEqual(len(list((self.package / "library" / "materials").glob("*.json"))), 3)

    def test_the_package_leaves_the_source_the_recipes_and_the_build_behind(self):
        self.assertIsNotNone(self.package, self.steps[-1].stdout)
        for part in ("src", "build", "CMakeLists.txt", "library/meshes", "logs", "assets/logo/_preview", "dist"):
            self.assertFalse((self.package / part).exists(), part)
        self.assertEqual([f.name for f in self.package.rglob("*.loaded.*")], [])
        self.assertFalse((self.package / "bin" / "game.built").exists(), "the editor's reload stamp")

    def test_the_debug_info_is_split_out_beside_the_package(self):
        self.assertIsNotNone(self.package, self.steps[-1].stdout)
        if not shutil.which("objcopy"):
            self.skipTest("no objcopy here, so the package keeps its debug info")
        symbols = self.package.parent / (self.package.name + "-symbols")
        self.assertTrue((symbols / f"{GAME}.debug").is_file())
        self.assertTrue((symbols / f"{engine.MODULE}.debug").is_file())


if __name__ == "__main__":
    _vkm.needs_the_build_vkm_drives()
    # A test never downloads: the pinned CMake and Ninja a module builds with must be here already.
    if not all(tool.present() for tool in toolchain.build_tools(engine.development())):
        _vkm.skip("the pinned CMake and Ninja are not fetched yet (`vkm toolchain`), and a test downloads nothing")
    _vkm.main()
