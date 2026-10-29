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
GENERATED = ROOT / "src" / "remote" / "generated"
GRPC_TAG = "v1.85.0"


def run(*args):
    print("REAPI ·", " ".join(map(str, args)), flush=True)
    subprocess.run(args, cwd=ROOT, check=True)


def main():
    cmake = shutil.which("cmake")
    compiler = shutil.which(os.environ.get("CXX", "clang++"))
    if not cmake:
        raise SystemExit("error: cmake is required to configure the pinned gRPC source")
    if not compiler:
        raise SystemExit("error: clang++ is required to compile REAPI protobuf sources")

    WORK.mkdir(parents=True, exist_ok=True)
    if not SOURCE.exists():
        run("git", "clone", "--branch", GRPC_TAG, "--depth", "1",
            "--recurse-submodules", "--shallow-submodules",
            "https://github.com/grpc/grpc.git", str(SOURCE))

    run(
        cmake, "-S", str(SOURCE), "-B", str(BUILD),
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
    run(cmake, "--build", str(BUILD), "--parallel", str(os.cpu_count() or 4))
    run(cmake, "--install", str(BUILD))

    protoc = PREFIX / "bin" / "protoc"
    plugin = PREFIX / "bin" / "grpc_cpp_plugin"
    if not protoc.exists() or not plugin.exists():
        raise SystemExit("error: static gRPC build did not install protoc and grpc_cpp_plugin")

    run(sys.executable, str(ROOT / "scripts" / "generate_reapi.py"),
        str(protoc), str(plugin), str(PREFIX / "include"))

    # Protobuf-generated .cc files must remain separate translation units:
    # each one defines file-level descriptor globals with overlapping names.
    generated_sources = sorted(
        path for path in GENERATED.rglob("*.pb.cc")
    ) + sorted(
        path for path in GENERATED.rglob("*.grpc.pb.cc")
    )
    if not generated_sources:
        raise SystemExit("error: protoc generated no C++ sources")

    object_dir = WORK / "generated-objects"
    if object_dir.exists():
        shutil.rmtree(object_dir)
    object_dir.mkdir(parents=True)

    generated_objects = []
    for index, source in enumerate(generated_sources):
        obj = object_dir / f"{index:04d}.o"
        run(
            compiler, "-std=c++17", "-O2",
            "-I" + str(PREFIX / "include"),
            "-I" + str(GENERATED),
            "-c", str(source), "-o", str(obj),
        )
        generated_objects.append(obj)

    archives = sorted((PREFIX / "lib").glob("*.a"))
    if not archives:
        raise SystemExit("error: static gRPC build produced no installed .a libraries")

    OUTPUT.mkdir(parents=True, exist_ok=True)
    bundle = OUTPUT / "libnightbuild_reapi.a"
    if bundle.exists():
        bundle.unlink()
    run("/usr/bin/libtool", "-static", "-o", str(bundle),
        *map(str, archives), *map(str, generated_objects))
    print("REAPI · static bundle:", bundle, flush=True)


if __name__ == "__main__":
    main()
