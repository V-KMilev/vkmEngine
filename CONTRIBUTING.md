# Contributing to vkmEngine

Thank you for wanting to help. This page is for changing the engine itself. If you
are making a game with it, [Getting started](docs/getting-started.md) is what you
want, and a bug or a question goes in an
[issue](https://github.com/V-KMilev/vkmEngine/issues).

## Before you write code

Open an issue for anything bigger than a fix, so the shape is agreed before the work
is done. The engine has opinions, written down in [docs/guides/](docs/guides/README.md):
read its README first, then [engine.md](docs/guides/engine.md), whose table of
decisions lists what was built, measured and turned down - temporal AA, a render
graph, a scripting language and more - and what would reopen each. A proposal that
meets one of those conditions is welcome; one that does not starts from that table.

## Build and test

```sh
git clone --recursive https://github.com/V-KMilev/vkmEngine
cd vkmEngine
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

You need CMake 3.25+, Ninja and Python 3.8+; the configure fetches the GCC every
release is built with. [Building](docs/reference/building.md) has the rest: other
compilers, the suites, sanitizers, and how to measure a change.

The `docs` suite checks more than the docs - code layout, comments, line width and
that the manual matches the source - so a change that passes it reads like the rest
of the tree.

## What a pull request needs

What [design.md](docs/guides/design.md#5-what-finished-means) calls finished: a clean
build and passing suites, a test you saw fail without your fix, the change used and
looked at, the docs updated in the same commit, and short commit messages that say
why. [code-style.md](docs/guides/code-style.md) covers the mechanics. CI builds and
tests every pull request on Linux and Windows, with GCC and Clang.

One subject per pull request, in as few commits as tell it.

## The license

vkmEngine is source-available under the [vkmEngine License](LICENSE), not an open-source
license. By submitting a contribution you grant the licensor the license its
Contributions section describes, and confirm the work is yours to give. Changed engine
code may be shared only as a contribution here or in a fork within this repository's
GitHub fork network.
