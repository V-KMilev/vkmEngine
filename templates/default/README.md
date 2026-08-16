# A vkmEngine project

    vkm build .     compile src/ into bin/game
    vkm run .       play it
    vkm edit .      open it in the editor
    vkm cook .      bake assets (needs no window)
    vkm package .   assemble a standalone game

## What is here

    project.json    what the game is called, and which scene it opens
    CMakeLists.txt  finds the engine and compiles src/ into one module
    src/            your gameplay code
    assets/         your art
    scenes/         your saved scenes

`src/module.cpp` holds the two entry points a host looks for.
`vkmRegisterBehaviors` is required. `vkmBuildScene` is optional: it builds a
world in code, and a project that authors scenes in the editor sets
`entryScene` in `project.json` instead.

## One thing that will catch you

Forward is `+Z`, not `-Z`. A sun overhead needs a **positive** pitch, and
`glm::quatLookAt` aims 180 degrees the other way. Nothing errors when you get
this wrong - it just looks wrong.
