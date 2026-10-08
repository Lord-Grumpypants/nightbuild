#!/usr/bin/env python3

from pathlib import Path

import os
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
PROFDATA = ROOT / "nightbuild.profdata"
NINJA_URL = "https://github.com/ninja-build/ninja.git"


def run(*args, cwd=None, env=None):
    print("PGO ·", " ".join(str(x) for x in args))
    subprocess.run(args, cwd=cwd, env=env, check=True)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: generate_pgo.py <nightbuild-pgo>"
        )

    nightbuild = Path(sys.argv[1]).resolve()

    if not nightbuild.exists():
        raise SystemExit(
            f"error: {nightbuild} not found; build nightbuild-pgo first"
        )

    git = shutil.which("git")

    llvm_profdata = shutil.which("llvm-profdata")

    if git is None:
        raise SystemExit("error: git not found")

    if llvm_profdata is None:
        candidate = Path("/opt/homebrew/opt/llvm/bin/llvm-profdata")

        if candidate.exists():
            llvm_profdata = str(candidate)
        else:
            raise SystemExit("error: llvm-profdata not found")

    workdir = Path(
        tempfile.mkdtemp(
            prefix="nightbuild-pgo-ninja-",
            dir="/tmp",
        )
    )

    ninja_src = workdir / "ninja"

    try:
        run(
            git,
            "clone",
            "--depth",
            "1",
            NINJA_URL,
            str(ninja_src),
        )

        run(
            sys.executable,
            "configure.py",
            cwd=ninja_src,
        )

        browse_header = ninja_src / "build" / "browse_py.h"

        run(
            "sh",
            "-c",
            "mkdir -p build && "
            "src/inline.sh kBrowsePy < src/browse.py > build/browse_py.h",
            cwd=ninja_src,
        )

        if not browse_header.exists():
            raise SystemExit(
                f"error: failed to generate {browse_header}"
            )

        print(f"PGO · generated {browse_header}")

        build_file = ninja_src / "BUILD.nb"

        build_file.write_text(
            """
project("Ninja")

executable("ninja"):
    sources = [
        "src/browse.cc",
        "src/depfile_parser.cc",
        "src/lexer.cc",
        "src/build.cc",
        "src/build_log.cc",
        "src/clean.cc",
        "src/clparser.cc",
        "src/debug_flags.cc",
        "src/deps_log.cc",
        "src/disk_interface.cc",
        "src/dyndep.cc",
        "src/dyndep_parser.cc",
        "src/edit_distance.cc",
        "src/elide_middle.cc",
        "src/eval_env.cc",
        "src/explanations.cc",
        "src/graph.cc",
        "src/graphviz.cc",
        "src/jobserver.cc",
        "src/jobserver_pool.cc",
        "src/json.cc",
        "src/line_printer.cc",
        "src/manifest_parser.cc",
        "src/metrics.cc",
        "src/missing_deps.cc",
        "src/parser.cc",
        "src/real_command_runner.cc",
        "src/state.cc",
        "src/status_printer.cc",
        "src/string_piece_util.cc",
        "src/util.cc",
        "src/version.cc",
        "src/jobserver-posix.cc",
        "src/subprocess-posix.cc",
        "src/ninja.cc",
    ]

    include_dirs = [
        ".",
    ]

    cflags = [
        "-std=c++17",
        "-O2",
        "-DNDEBUG",
        "-Wall",
        "-Wextra",
        "-Wno-deprecated",
        "-Wno-missing-field-initializers",
        "-Wno-unused-parameter",
        "-fno-rtti",
        "-fno-exceptions",
        "-fvisibility=hidden",
        "-pipe",
        "-DNINJA_HAVE_BROWSE",
        "-DNINJA_PYTHON=\\"python3\\"",
    ]

    ldflags = [
        "-L/opt/homebrew/opt/llvm/lib",
    ]

    frameworks = [
    ]
""",
            encoding="utf-8",
        )

        print(f"PGO · generated {build_file}")

        profile_pattern = workdir / "nightbuild-%p.profraw"

        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = str(profile_pattern)

        run(
            str(nightbuild),
            "gen",
            "-C",
            "build",
            cwd=ninja_src,
            env=env,
        )

        run(
            str(nightbuild),
            "build",
            "-C",
            "build",
            cwd=ninja_src,
            env=env,
        )

        raw_profiles = sorted(
            workdir.glob("nightbuild-*.profraw")
        )

        if not raw_profiles:
            raise SystemExit(
                "error: NightBuild produced no .profraw files"
            )

        merge_args = [
            llvm_profdata,
            "merge",
            "-output",
            str(PROFDATA),
            *map(str, raw_profiles),
        ]

        run(*merge_args)

        print(f"PGO · wrote {PROFDATA}")

    finally:
        print(f"PGO · training directory: {workdir}")


if __name__ == "__main__":
    main()