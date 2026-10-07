#!/usr/bin/env python3

from pathlib import Path

import os
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent
BUILD_DIR = ROOT / "out" / "Official"
BREW_PREFIX = Path("/opt/homebrew")
BREW = BREW_PREFIX / "bin" / "brew"
LLVM_PREFIX = BREW_PREFIX / "opt" / "llvm"
LLVM_BIN = LLVM_PREFIX / "bin"


def run(*args, cwd=None, env=None):
    print("Bootstrap ·", " ".join(str(arg) for arg in args))
    subprocess.run(args, cwd=cwd, env=env, check=True)


def command_exists(command):
    return shutil.which(command) is not None


def install_homebrew():
    if command_exists("brew"):
        return

    print("Bootstrap · installing Homebrew")

    run(
        "/bin/bash",
        "-c",
        'curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh | /bin/bash',
    )


def brew_path():
    if command_exists("brew"):
        return shutil.which("brew")

    if BREW.exists():
        return str(BREW)

    raise SystemExit("error: Homebrew installation completed but brew was not found")


def install_packages(brew):
    packages = [
        "llvm",
        "notcurses",
        "python@3.14",
        "git",
    ]

    run(
        brew,
        "install",
        *packages,
    )


def make_environment():
    env = os.environ.copy()

    llvm_bin = str(LLVM_BIN)

    env["PATH"] = (
        llvm_bin
        + os.pathsep
        + str(BREW_PREFIX / "bin")
        + os.pathsep
        + env.get("PATH", "")
    )

    env["CC"] = str(LLVM_BIN / "clang")
    env["CXX"] = str(LLVM_BIN / "clang++")
    env["AR"] = str(LLVM_BIN / "llvm-ar")
    env["LD"] = str(LLVM_BIN / "ld.lld")

    return env


def main():
    install_homebrew()

    brew = brew_path()

    install_packages(brew)

    env = make_environment()

    llvm_profdata = LLVM_BIN / "llvm-profdata"
    clang = LLVM_BIN / "clang"
    clangxx = LLVM_BIN / "clang++"
    lld = LLVM_BIN / "ld.lld"

    required = [
        clang,
        clangxx,
        lld,
        llvm_profdata,
    ]

    missing = [
        str(path)
        for path in required
        if not path.exists()
    ]

    if missing:
        raise SystemExit(
            "error: Homebrew LLVM installation is missing:\n"
            + "\n".join(f"  {path}" for path in missing)
        )

    run(
        "make",
        cwd=ROOT,
        env=env,
    )

    nightbuild = ROOT / "nightbuild"

    if not nightbuild.exists():
        raise SystemExit(
            f"error: {nightbuild} was not produced by make"
        )

    run(
        str(nightbuild),
        "gen",
        "-C",
        str(BUILD_DIR),
        cwd=ROOT,
        env=env,
    )

    run(
        str(nightbuild),
        "build",
        "-C",
        str(BUILD_DIR),
        "nightbuild-pgo",
        cwd=ROOT,
        env=env,
    )

    python = shutil.which("python3.14", path=env["PATH"])

    if python is None:
        raise SystemExit(
            "error: python3.14 was not found after Homebrew installation"
        )

    run(
        python,
        str(ROOT / "scripts" / "generate_pgo.py"),
        cwd=ROOT,
        env=env,
    )


if __name__ == "__main__":
    main()