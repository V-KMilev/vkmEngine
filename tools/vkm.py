#!/usr/bin/env python3
"""vkm - the project tool for vkmEngine: build, run and ship a game project.

Every command that starts the game builds the module and cooks the assets first,
so what runs is what is on disk. The engine is found beside this script, in an
installed SDK or the engine's own tree. `vkm -h` lists the commands; the work is
in vkmcli/, one module a concern.
"""

import sys

from vkmcli.cli import main

if __name__ == "__main__":
    sys.exit(main())
