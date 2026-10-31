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

GRPC_WORK = ROOT / "third_party" / "grpc-work"
GRPC_SOURCE = GRPC_WORK / "grpc"
GRPC_BUILD = GRPC_WORK / "grpc-build"
GRPC_INSTALL = GRPC_WORK / "install"
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
        "cmake",
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


def build_grpc_codegen(env):
    cmake = shutil.which("cmake", path=env["PATH"])
    if cmake is None:
        raise SystemExit("error: CMake was not found after Homebrew installation")

    # Request the codemodel before configuring so the target graph can be
    # exported directly from this exact configuration.
    query_dir = GRPC_BUILD / ".cmake" / "api" / "v1" / "query"
    query_dir.mkdir(parents=True, exist_ok=True)
    (query_dir / "codemodel-v2").touch()

    run(
        cmake,
        "-S",
        str(GRPC_SOURCE),
        "-B",
        str(GRPC_BUILD),
        "-G",
        "Unix Makefiles",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_C_COMPILER={LLVM_BIN / 'clang'}",
        f"-DCMAKE_CXX_COMPILER={LLVM_BIN / 'clang++'}",
        f"-DCMAKE_INSTALL_PREFIX={GRPC_INSTALL}",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DgRPC_BUILD_TESTS=OFF",
        "-DgRPC_INSTALL=ON",
        "-DgRPC_BUILD_CODEGEN=ON",
        "-DgRPC_BUILD_GRPC_CPP_PLUGIN=ON",
        "-DgRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF",
        "-DgRPC_BUILD_GRPC_NODE_PLUGIN=OFF",
        "-DgRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN=OFF",
        "-DgRPC_BUILD_GRPC_PHP_PLUGIN=OFF",
        "-DgRPC_BUILD_GRPC_PYTHON_PLUGIN=OFF",
        "-DgRPC_BUILD_GRPC_RUBY_PLUGIN=OFF",
        "-DgRPC_BUILD_GRPC_OTEL_PLUGIN=OFF",
        "-DgRPC_ABSL_PROVIDER=module",
        "-DgRPC_CARES_PROVIDER=module",
        "-DgRPC_PROTOBUF_PROVIDER=module",
        "-DgRPC_RE2_PROVIDER=module",
        "-DgRPC_SSL_PROVIDER=module",
        "-DgRPC_ZLIB_PROVIDER=module",
        "-Dprotobuf_BUILD_PROTOC_BINARIES=ON",
        "-Dprotobuf_BUILD_PROTOBUF_BINARIES=ON",
        "-Dprotobuf_BUILD_LIBPROTOC=OFF",
        "-Dprotobuf_BUILD_TESTS=OFF",
        "-DABSL_BUILD_TESTING=OFF",
        "-DABSL_BUILD_TEST_HELPERS=OFF",
        "-DRE2_BUILD_TESTING=OFF",
        "-DZLIB_BUILD_EXAMPLES=OFF",
        cwd=ROOT,
        env=env,
    )

    run(
        cmake,
        "--build",
        str(GRPC_BUILD),
        "--config",
        "Release",
        "--target",
        "protoc",
        "grpc_cpp_plugin",
        "--parallel",
        cwd=ROOT,
        env=env,
    )

    inventory = GRPC_BUILD / "nightbuild-cmake-inventory.json"

    run(
        sys.executable,
        str(ROOT / "scripts" / "export_cmake_file_api.py"),
        "--reply-dir",
        str(GRPC_BUILD / ".cmake" / "api" / "v1" / "reply"),
        "--output",
        str(inventory),
        "--configuration",
        "Release",
        "--root",
        "grpc++",
        cwd=ROOT,
        env=env,
    )

    run(
        sys.executable,
        str(ROOT / "scripts" / "generate_grpc_build_nb.py"),
        str(inventory),
        "--output",
        str(ROOT / "third_party" / "grpc.BUILD.nb"),
        "--no-backup",
        cwd=ROOT,
        env=env,
    )


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
    build_grpc_codegen(env)

    run(
        sys.executable,
        str(ROOT / "scripts" / "build_reapi.py"),
        cwd=ROOT,
        env=env,
    )

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
