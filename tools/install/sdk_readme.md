# vkmEngine

A C++ game engine: an editor, a runtime, and `vkm`, the command that builds,
runs and packages your game.

## Start

Open the editor - from your app menu if you used the installer, or
`bin/vkm_editor` here - and pick New Project, or one of the examples.

Or from a terminal:

```sh
vkm new mygame                  # a project from the default template
vkm new lab -t physics_lab      # a copy of an example
cd mygame
vkm run                         # build, cook and play it
vkm edit                        # open it in the editor
vkm package                     # the game a player gets, under dist/
```

The first build downloads the compiler, CMake and Ninja this engine is built
with (about 250 MB, once). `vkm doctor` checks everything and says what to fix.

## What is here

```
vkm, vkm.cmd     the command
bin/             the editor, runtime, cooker and server
examples/        projects to learn from; `vkm new -t <name>` copies one
templates/       what a new project starts from
shipping/        the engine a packaged game runs on
include/ lib/    what your game's code compiles against
python/          the Python vkm runs on
shaders/ assets/ the engine's own data
```

The manual: https://vkmengine.com/docs/
