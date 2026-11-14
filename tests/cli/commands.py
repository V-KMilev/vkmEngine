"""vkm's commands that need no compiler: it names its engine, refuses what it cannot do, reads a project
and cleans up after itself."""

import json
import unittest

import _vkm
from _vkm import vkm


class Commands(unittest.TestCase):
    def test_version_names_the_engine_it_drives(self):
        ran = vkm("--version")
        self.assertEqual(ran.returncode, 0, ran.stdout)
        self.assertRegex(ran.stdout, r"^vkm for vkmEngine \d+\.\d+\.\d+ at ")
        self.assertIn(str(_vkm.ENGINE), ran.stdout)

    def test_an_unknown_command_is_refused(self):
        ran = vkm("frobnicate")
        self.assertNotEqual(ran.returncode, 0)
        self.assertIn("invalid choice: 'frobnicate'", ran.stdout)

    def test_a_folder_that_is_no_project_is_refused_saying_so(self):
        empty = _vkm.scratch("not_a_project")
        for command in ("cook", "doctor", "clean"):
            ran = vkm(command, empty)
            if command == "doctor":
                # Doctor checks the engine anyway, and says it found no project.
                self.assertIn("none here; name one, or make one with `vkm new <name>`", ran.stdout)
                continue
            self.assertEqual(ran.returncode, 1, ran.stdout)
            self.assertIn(f"no project.json in '{empty}' or any directory above it", ran.stdout)

    def test_new_copies_a_template_under_the_folder_name_and_refuses_a_folder_in_use(self):
        where = _vkm.scratch("new")
        ran = vkm("new", where / "mygame")
        self.assertEqual(ran.returncode, 0, ran.stdout)
        made = json.loads((where / "mygame" / "project.json").read_text())
        self.assertEqual(made["name"], "mygame")
        self.assertRegex(made["engineVersion"], r"^\d+\.\d+\.\d+$")
        for part in ("CMakeLists.txt", ".gitignore", "src/module.cpp", "scenes/main.json"):
            self.assertTrue((where / "mygame" / part).is_file(), part)

        again = vkm("new", where / "mygame")
        self.assertEqual(again.returncode, 1)
        self.assertIn("already exists and is not empty", again.stdout)
        unknown = vkm("new", where / "other", "-t", "no_such_template")
        self.assertEqual(unknown.returncode, 1)
        self.assertIn("no template or example called 'no_such_template'", unknown.stdout)
        self.assertFalse((where / "other").exists())

    def test_doctor_reads_the_project_it_is_given(self):
        where = _vkm.scratch("doctor")
        self.assertEqual(vkm("new", where / "checked").returncode, 0)
        ran = vkm("doctor", where / "checked" / "scenes")
        self.assertIn("project checked", ran.stdout)
        self.assertIn(f"[ok] directory     {where / 'checked'}", ran.stdout)
        self.assertIn("[--] module", ran.stdout)
        self.assertIn("[ok] entry scene   scenes/main.json", ran.stdout)

    def test_clean_deletes_what_the_tools_wrote_and_all_of_it_with_all(self):
        where = _vkm.scratch("clean")
        project = where / "tidy"
        self.assertEqual(vkm("new", project).returncode, 0)
        made = ("build/development/x.o", "bin/libgame.so", "cooked/_manifest.json", "logs/log.log", "dist/x/game")
        for path in made:
            (project / path).parent.mkdir(parents=True, exist_ok=True)
            (project / path).write_text("x")

        ran = vkm("clean", project)
        self.assertEqual(ran.returncode, 0, ran.stdout)
        for part in ("build", "bin", "cooked", "logs"):
            self.assertFalse((project / part).exists(), part)
        self.assertTrue((project / "dist").is_dir())
        self.assertIn("dist/ is kept", ran.stdout)

        ran = vkm("clean", project, "--all")
        self.assertEqual(ran.returncode, 0, ran.stdout)
        self.assertFalse((project / "dist").exists())
        # What the user made stays.
        for part in ("src/module.cpp", "scenes/main.json", "library", "project.json"):
            self.assertTrue((project / part).exists(), part)


if __name__ == "__main__":
    _vkm.needs_the_build_vkm_drives()
    _vkm.main()
