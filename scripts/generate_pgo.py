# scripts/generate_pgo.py
#!/usr/bin/env python3

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
NIGHTBUILD = ROOT / "nightbuild-pgo"
PROFDATA = ROOT / "nightbuild.profdata"
NINJA_URL = "https://github.com/ninja-build/ninja.git"


def run(*args, cwd=None, env=None):
    print("PGO ·", " ".join(str(x) for x in args))
    subprocess.run(args, cwd=cwd, env=env, check=True)


def main():
    if not NIGHTBUILD.exists():
        raise SystemExit(
            f"error: {NIGHTBUILD} not found; build nightbuild-pgo first"
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

    workdir = Path(tempfile.mkdtemp(prefix="nightbuild-pgo-ninja-", dir="/tmp"))
    ninja_src = workdir / "ninja"

    try:
        # Fresh Ninja checkout. The entire training tree is disposable.
        run(
            git,
            "clone",
            "--depth",
            "1",
            NINJA_URL,
            str(ninja_src),
        )

        # Ninja's configure step generates config.h and the normal Ninja
        # build description. NightBuild itself performs the actual compile.
        run(
            sys.executable,
            "configure.py",
            cwd=ninja_src,
        )

        sources = sorted(
            path.relative_to(ninja_src)
            for path in (ninja_src / "src").glob("*.cc")
        )

        if not sources:
            raise SystemExit("error: no Ninja C++ sources found")

        build_file = ninja_src / "BUILD.nb"

        source_lines = ",\n".join(
            f'        "{source}",'
            for source in sources
        )

        build_file.write_text(
            f'''project("Ninja")

executable("ninja"):
    sources = [
{source_lines}
    ]

    cxxflags = [
        "-std=c++17",
        "-O3",
    ]
''',
            encoding="utf-8",
        )

        # Keep every raw profile separate. LLVM's %p expands to the process ID,
        # which is important because NightBuild launches multiple compilers.
        profile_pattern = workdir / "nightbuild-%p.profraw"

        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = str(profile_pattern)

        # Generate the concrete NightBuild command database.
        run(
            str(NIGHTBUILD),
            "gen",
            "-C",
            "build",
            cwd=ninja_src,
            env=env,
        )

        # This is the actual PGO workload: NightBuild compiles Ninja.
        run(
            str(NIGHTBUILD),
            "build",
            "-C",
            "build",
            cwd=ninja_src,
            env=env,
        )

        raw_profiles = sorted(workdir.glob("nightbuild-*.profraw"))

        if not raw_profiles:
            raise SystemExit("error: NightBuild produced no .profraw files")

        # Merge all compiler/process profiles into the profile consumed by
        # the normal NightBuild target.
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
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    main()


