# Contributing to vkmEngine

Thank you for wanting to help. This page is for changing the engine itself. If you
are making a game with it, [Getting started](docs/getting-started.md) is what you
want, and a bug or a question goes in an
[issue](https://github.com/V-KMilev/vkmEngine/issues).

A change to one of the engine's libraries - vkmLog, vkmPlatform, vkmAudio, vkmNetwork, vkmPhysics,
vkmOpenGL or vkmVulkan, each a submodule under `modules/` - goes to that library's own repository, whose
CONTRIBUTING.md says how; then an engine commit moves the pin.

## Before you write code

Open an issue for anything bigger than a fix, so the shape is agreed before the work
is done. The engine has opinions: read [Architecture](docs/reference/architecture.md),
then [Decisions](docs/reference/decisions.md), whose table lists what was built, measured
and turned down - temporal AA, a render graph, a scripting language and more - and what
would reopen each. A proposal that meets one of those conditions is welcome; one that does
not starts from that table.

## Build and test

```sh
git clone --recursive https://github.com/V-KMilev/vkmEngine
cd vkmEngine
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

You need CMake 3.25+, Ninja, Python 3.8+ and Git LFS, installed before the clone; the
configure fetches the GCC every release is built with. [Building](docs/reference/building.md) has the rest: other
compilers, the suites, sanitizers, and how to measure a change.

The `docs` suite checks more than the docs - American spelling, a graphics API named only in a
backend's places, comments included, the engine's layer order and more
([Building](docs/reference/building.md#tests) lists them) - so a change that passes it keeps the
tree's rules.

## What a pull request needs

A clean build with no warnings and passing suites, a test you saw fail without your fix,
the change used and looked at, and the docs updated in the same commit. A performance change
carries [its measurement](docs/reference/building.md#measuring-a-change). A change to a scene,
prefab, project or cooked format goes to the owner first.

CI runs only by hand (`.github/workflows/ci.yml`), not on a pull request: it builds and tests on
Linux and Windows, with GCC and Clang.

A commit message is lowercase `type(scope): summary` under 72 characters, then a few lines on
what the change does as a whole and why. The diff says the rest. One subject per pull request,
in as few commits as tell it.

## How the code looks

Match the files around your change: no formatter holds the layout, so review does.

- **Files:** `#pragma once`, ASCII, 4 spaces, opening brace on the same line, 110 columns. In a
  `.cpp` the own header comes first, then standard, third-party and local includes, each group
  apart.
- **Names:** `PascalCase` types, `camelCase` functions and locals, `UPPER_SNAKE_CASE` constants,
  `snake_case` files, and American spelling (`color`).
- **Classes and structs:** a struct is data, with bare members. A class has behavior and hides
  state: `m_` members, data last in a `private:` section of its own, every override marked
  `override`, and all five special members written out.
- **Helpers:** a function only one `.cpp` uses sits in an anonymous namespace, not `static`.
- **Lists:** a parameter, argument or braced list sits on one line when it fits, and otherwise
  breaks whole, one item per line, with no comment inside an argument list.
- **Comments:** few and short. A comment says what a reader would otherwise ask, and states what
  is true, never what used to be. A long explanation belongs in `docs/reference/`.

[Architecture](docs/reference/architecture.md#adding-to-the-engine) says where new code belongs and
what shape it takes.

## The license

vkmEngine is source-available under the [vkmEngine License](LICENSE), not an open-source
license. By submitting a contribution you grant the licensor the license its
Contributions section describes, and confirm the work is yours to give. Changed engine
code may be shared only as a contribution to the repository of the code you changed, here or a
library's, or in a fork within that repository's GitHub fork network.
