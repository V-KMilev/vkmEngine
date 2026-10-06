# Changelog

Notable changes to vkmEngine, newest first. Each release is tagged `vX.Y.Z`.

## 1.0.1

- **Every release comes built with GCC and with Clang**, on Linux and Windows alike, each
  bringing the compiler it was built with: GCC 15.2 or Clang 21.1. The installers take GCC;
  `VKM_COMPILER=clang` before them takes Clang.
- **The editor builds your game.** New Project, File > Build Scripts and Package Game run
  vkm, their output in a new Build tab, and a project whose code was never built is built
  as it opens - no terminal needed.
- **Building the engine uses a pinned compiler** when none is named, so what you build is
  what ships.
- New projects get their `.gitignore` from the one list of what the tools generate.
- CI builds, tests, packages and uses all four SDKs on every push, and caches compiles
  between runs.
- `CONTRIBUTING.md` says how to build, test and send a change.
