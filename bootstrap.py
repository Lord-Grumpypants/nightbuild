#!/usr/bin/env python3

from pathlib import Path

import os
import shutil
import subprocess


ROOT = Path(__file__).resolve().parent

OFFICIAL_BUILD_DIR = ROOT / "out" / "Official"
PGO_BUILD_DIR = ROOT / "out" / "PGO"

BREW_PREFIX = Path("/opt/homebrew")
BREW = BREW_PREFIX / "bin" / "brew"

LLVM_PREFIX = BREW_PREFIX / "opt" / "llvm"
LLVM_BIN = LLVM_PREFIX / "bin"

LLD_PREFIX = BREW_PREFIX / "opt" / "lld"
LLD_BIN = LLD_PREFIX / "bin"

GRPC_WORK = ROOT / "third_party" / "grpc-work"
GRPC_SOURCE = GRPC_WORK / "grpc"
GRPC_TAG = "v1.85.0"


def run(*args, cwd=None, env=None):
    print("Bootstrap ·", " ".join(str(arg) for arg in args), flush=True)
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

    run(brew, "install", *missing)


def make_environment():
    env = os.environ.copy()

    env["PATH"] = os.pathsep.join([
        str(LLVM_BIN),
        str(LLD_BIN),
        str(BREW_PREFIX / "bin"),
        env.get("PATH", ""),
    ])

    env["CC"] = str(LLVM_BIN / "clang")
    env["CXX"] = str(LLVM_BIN / "clang++")
    env["AR"] = str(LLVM_BIN / "llvm-ar")

    return env


def ensure_grpc_source():
    GRPC_WORK.mkdir(parents=True, exist_ok=True)

    if not GRPC_SOURCE.exists():
        run(
            "git",
            "clone",
            "--branch",
            GRPC_TAG,
            "--depth",
            "1",
            "--recurse-submodules",
            "--shallow-submodules",
            "https://github.com/grpc/grpc.git",
            str(GRPC_SOURCE),
        )
    else:
        run(
            "git",
            "submodule",
            "update",
            "--init",
            "--recursive",
            cwd=GRPC_SOURCE,
        )


def verify_generated_reapi():
    generated = ROOT / "src" / "remote" / "generated"
    manifest = ROOT / "third_party" / "reapi.generated.BUILD.nb"

    required = [
        generated / "build/bazel/remote/execution/v2/remote_execution.pb.cc",
        generated / "build/bazel/remote/execution/v2/remote_execution.pb.h",
        generated / "build/bazel/remote/execution/v2/remote_execution.grpc.pb.cc",
        generated / "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h",
        generated / "build/bazel/semver/semver.pb.cc",
        generated / "build/bazel/semver/semver.pb.h",
        generated / "build/bazel/semver/semver.grpc.pb.cc",
        generated / "build/bazel/semver/semver.grpc.pb.h",
        manifest,
    ]

    missing = [str(path.relative_to(ROOT)) for path in required if not path.is_file()]
    if missing:
        raise SystemExit(
            "error: committed REAPI generated files are missing:\n"
            + "\n".join(f"  {path}" for path in missing)
        )

    sources = list(generated.rglob("*.pb.cc"))
    if len(sources) != 30:
        raise SystemExit(
            f"error: expected 30 generated REAPI translation units, found {len(sources)}"
        )

    print("Bootstrap · verified 30 committed REAPI translation units")


def main():
    install_homebrew()

    brew = brew_path()
    install_packages(brew)

    env = make_environment()

    required = [
        LLVM_BIN / "clang",
        LLVM_BIN / "clang++",
        LLVM_BIN / "llvm-ar",
        LLVM_BIN / "llvm-profdata",
        LLD_BIN / "ld64.lld",
    ]

    missing = [str(path) for path in required if not path.exists()]
    if missing:
        raise SystemExit(
            "error: required Homebrew LLVM/LLD tools are missing:\n"
            + "\n".join(f"  {path}" for path in missing)
        )

    run(
        "git",
        "submodule",
        "update",
        "--init",
        "--recursive",
        cwd=ROOT,
        env=env,
    )

    run("make", cwd=ROOT, env=env)

    nightbuild = ROOT / "nightbuild"
    if not nightbuild.is_file():
        raise SystemExit(f"error: {nightbuild} was not produced by make")

    ensure_grpc_source()
    verify_generated_reapi()

    run(
        str(nightbuild),
        "gen",
        "-C",
        str(PGO_BUILD_DIR),
        "--no-license",
        cwd=ROOT,
        env=env,
    )

    run(
        str(nightbuild),
        "build",
        "-C",
        str(PGO_BUILD_DIR),
        "nightbuild-pgo",
        "--no-license",
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
        str(ROOT / "nightbuild-pgo"),
        cwd=ROOT,
        env=env,
    )

    run(
        str(nightbuild),
        "gen",
        "-C",
        str(OFFICIAL_BUILD_DIR),
        "--no-license",
        cwd=ROOT,
        env=env,
    )

    run(
        str(nightbuild),
        "build",
        "-C",
        str(OFFICIAL_BUILD_DIR),
        "nightbuild",
        "--no-license",
        cwd=ROOT,
        env=env,
    )

    run(
        str(nightbuild),
        "build",
        "-C",
        str(OFFICIAL_BUILD_DIR),
        "remote-capabilities",
        "--no-license",
        cwd=ROOT,
        env=env,
    )


if __name__ == "__main__":
    main()
