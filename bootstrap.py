#!/usr/bin/env python3

from pathlib import Path

import os
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent

OFFICIAL_BUILD_DIR = ROOT / "out" / "Official"
PGO_BUILD_DIR = ROOT / "out" / "PGO"

BREW_PREFIX = Path("/opt/homebrew")
BREW = BREW_PREFIX / "bin" / "brew"

LLVM_PREFIX = BREW_PREFIX / "opt" / "llvm"
LLVM_BIN = LLVM_PREFIX / "bin"

LLD_PREFIX = BREW_PREFIX / "opt" / "lld"
LLD_BIN = LLD_PREFIX / "bin"


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
        "curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh | /bin/bash",
    )


def brew_path():
    if command_exists("brew"):
        return shutil.which("brew")

    if BREW.exists():
        return str(BREW)

    raise SystemExit(
        "error: Homebrew installation completed but brew was not found"
    )


def package_installed(brew, package):
    result = subprocess.run(
        [brew, "list", "--formula", package],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    return result.returncode == 0


def install_packages(brew):
    packages = [
        "llvm",
        "lld",
        "notcurses",
        "python@3.14",
        "git",
    ]

    missing = [
        package
        for package in packages
        if not package_installed(brew, package)
    ]

    if not missing:
        print("Bootstrap · Homebrew packages already installed")
        return

    run(
        brew,
        "install",
        *missing,
    )


def make_environment():
    env = os.environ.copy()

    llvm_bin = str(LLVM_BIN)
    lld_bin = str(LLD_BIN)

    env["PATH"] = (
        llvm_bin
        + os.pathsep
        + lld_bin
        + os.pathsep
        + str(BREW_PREFIX / "bin")
        + os.pathsep
        + env.get("PATH", "")
    )

    env["CC"] = str(LLVM_BIN / "clang")
    env["CXX"] = str(LLVM_BIN / "clang++")
    env["AR"] = str(LLVM_BIN / "llvm-ar")

    return env


def main():
    install_homebrew()

    brew = brew_path()

    install_packages(brew)

    env = make_environment()

    clang = LLVM_BIN / "clang"
    clangxx = LLVM_BIN / "clang++"
    llvm_ar = LLVM_BIN / "llvm-ar"
    llvm_profdata = LLVM_BIN / "llvm-profdata"
    lld = LLD_BIN / "ld64.lld"

    required = [
        clang,
        clangxx,
        llvm_ar,
        llvm_profdata,
        lld,
    ]

    missing = [
        str(path)
        for path in required
        if not path.exists()
    ]

    if missing:
        raise SystemExit(
            "error: required Homebrew LLVM/LLD tools are missing:\n"
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
        str(PGO_BUILD_DIR),
        cwd=ROOT,
        env=env,
    )

    run(
        str(nightbuild),
        "build",
        "-C",
        str(PGO_BUILD_DIR),
        "nightbuild-pgo",
        cwd=ROOT,
        env=env,
    )

    python = shutil.which(
        "python3.14",
        path=env["PATH"],
    )

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

    run(
        str(nightbuild),
        "gen",
        "-C",
        str(OFFICIAL_BUILD_DIR),
        cwd=ROOT,
        env=env,
    )

    run(
        str(nightbuild),
        "build",
        "-C",
        str(OFFICIAL_BUILD_DIR),
        "nightbuild",
        cwd=ROOT,
        env=env,
    )


if __name__ == "__main__":
    main()