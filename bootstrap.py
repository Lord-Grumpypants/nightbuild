#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parent
NIGHTBUILD = ROOT / "nightbuild"
BUILD_DIR = ROOT / "out" / "Official"


def run(*args):
    print("Bootstrap ·", " ".join(str(arg) for arg in args))
    subprocess.run(args, cwd=ROOT, check=True)


def main():
    run("make")

    run(
        str(NIGHTBUILD),
        "gen",
        "-C",
        str(BUILD_DIR),
    )

    run(
        str(NIGHTBUILD),
        "build",
        "-C",
        str(BUILD_DIR),
        "nightbuild-pgo",
    )

    run(
        sys.executable,
        str(ROOT / "scripts" / "generate_pgo.py"),
    )


if __name__ == "__main__":
    main()