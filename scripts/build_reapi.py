#!/usr/bin/env python3
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build" / "reapi"
SOURCE = WORK / "grpc"
BUILD = WORK / "grpc-build"
PREFIX = WORK / "install"
OUTPUT = WORK / "lib"
GRPC_TAG = "v1.85.0"


def run(*args):
    print("REAPI ·", " ".join(map(str, args)), flush=True)
    subprocess.run(args, cwd=ROOT, check=True)


def main():
    if not shutil.which("cmake"):
        raise SystemExit("error: cmake is required to build the static REAPI transport")

    WORK.mkdir(parents=True, exist_ok=True)
    if not SOURCE.exists():
        run("git", "clone", "--branch", GRPC_TAG, "--depth", "1",
            "--recurse-submodules", "--shallow-submodules",
            "https://github.com/grpc/grpc.git", str(SOURCE))

    run(
        "cmake", "-S", str(SOURCE), "-B", str(BUILD),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_INSTALL_PREFIX=" + str(PREFIX),
        "-DCMAKE_OSX_ARCHITECTURES=arm64",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DgRPC_INSTALL=ON",
        "-DgRPC_BUILD_TESTS=OFF",
        "-DgRPC_BUILD_CODEGEN=ON",
        "-DgRPC_BUILD_GRPC_CPP_PLUGIN=ON",
        "-DgRPC_ABSL_PROVIDER=module",
        "-DgRPC_CARES_PROVIDER=module",
        "-DgRPC_PROTOBUF_PROVIDER=module",
        "-DgRPC_RE2_PROVIDER=module",
        "-DgRPC_SSL_PROVIDER=module",
        "-DgRPC_ZLIB_PROVIDER=module",
    )
    run("cmake", "--build", str(BUILD), "--parallel", str(os.cpu_count() or 4))
    run("cmake", "--install", str(BUILD))

    protoc = PREFIX / "bin" / "protoc"
    plugin = PREFIX / "bin" / "grpc_cpp_plugin"
    if not protoc.exists() or not plugin.exists():
        raise SystemExit("error: static gRPC build did not install protoc and grpc_cpp_plugin")

    run(sys.executable, str(ROOT / "scripts" / "generate_reapi.py"),
        str(protoc), str(plugin), str(PREFIX / "include"))

    archives = sorted(PREFIX.glob("lib/*.a"))
    if not archives:
        raise SystemExit("error: static gRPC build produced no installed .a libraries")

    OUTPUT.mkdir(parents=True, exist_ok=True)
    bundle = OUTPUT / "libnightbuild_reapi.a"
    if bundle.exists():
        bundle.unlink()
    run("/usr/bin/libtool", "-static", "-o", str(bundle), *map(str, archives))
    print("REAPI · static bundle:", bundle, flush=True)


if __name__ == "__main__":
    main()
