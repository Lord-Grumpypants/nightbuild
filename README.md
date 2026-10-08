# NightBuild

**A simple, fast, native C++23 build system with GN-style generation and Ninja-style execution.**

NightBuild is a native macOS build system designed to make project manifests easy to write while keeping build execution fast.

The goal is simple:

**Easy project description → concrete build graph → fast execution.**

No CMake language to learn. No enormous build-system configuration. No generated `build.ninja` sitting between your project and the executor.

## Quick Start

Clone NightBuild and run the bootstrap:

```bash
git clone https://github.com/Lord-Grumpypants/nightbuild.git
cd nightbuild
python3 bootstrap.py
```

## Why NightBuild?

C++ projects shouldn't need a complicated build system just to compile a few source files.

A small NightBuild project can look like this:

```text
project("Hello")

executable("hello"):
    sources = ["main.cpp"]
    cxxflags = ["-O3", "-flto=full"]
    ldflags = ["-flto=full"]
```

Then:

```bash
nightbuild gen -C out/Official
nightbuild build -C out/Official
```

That's it.

NightBuild turns the manifest into a concrete build graph during `gen`, then executes that graph directly during `build`.

## Features

* Native **C++23** implementation
* Simple indentation-based `BUILD.nb` manifests
* GN-style build arguments through `declare_args` and `args.nb`
* Fast parallel execution
* Dependency-aware scheduling
* Hash-based incremental builds
* Built-in C, C++, Objective-C, Objective-C++, and Swift toolchains
* Static libraries, shared libraries, executables, applications, frameworks, and object targets
* Generator and action targets
* Toolchain configuration through JSON
* `pkg-config` integration
* Compile command generation
* PGO/LTO-friendly
* No runtime dependency on Python, CMake, GN, or Ninja for native NightBuild builds

## Build Arguments

NightBuild supports configurable build arguments through `declare_args` and `args.nb`.

Arguments are declared in `BUILD.nb`:

```text
declare_args("lto"):
    default = true

if lto:
    ldflags += ["-flto=full"]
```

A project can declare as many arguments as it needs. Arguments have defaults, so a project remains usable even when no `args.nb` file exists.

Configuration overrides live in the output directory:

```text
out/Official/args.nb
```

For example:

```text
lto = false
```

Apply the configuration with:

```text
nightbuild args -C out/Official
```

Then build normally:

```text
nightbuild build -C out/Official
```

`args.nb` only overrides arguments explicitly declared by the project. Unknown arguments are rejected.

This keeps project configuration separate from the project manifest while allowing the same `BUILD.nb` to produce different build configurations.

The intended flow is:

```text
BUILD.nb
   ↓
declare_args + defaults
   ↓
args.nb overrides
   ↓
evaluate BUILD.nb
   ↓
concrete build graph
   ↓
build
```

Arguments configure the project; they do not modify targets after generation.

## A Slightly Larger Example

NightBuild supports normal target dependencies:

```text
project("Hello")

executable("hello"):
    sources = ["main.cpp"]
    deps = ["hellolib"]
    cxxflags = ["-O3", "-flto=full"]
    ldflags = ["-flto=full"]

static_library("hellolib"):
    sources = ["hello-lib.cpp"]
    cxxflags = ["-O3", "-flto=full"]
```

NightBuild understands that `hello` depends on `hellolib`, builds the library first, and then links the executable.

The resulting static library follows the conventional naming scheme:

```text
libhellolib.a
```

## Commands

Generate a build directory:

```text
nightbuild gen -C out/Official
```

Configure a build directory using `args.nb`:

```text
nightbuild args -C out/Official
```

Build everything:

```text
nightbuild build -C out/Official
```

Build a specific target:

```text
nightbuild build -C out/Official hello
```

Build multiple targets:

```text
nightbuild build -C out/Official nightbuild test
```

Rebuild:

```text
nightbuild rebuild -C out/Official
```

Clean generated objects:

```text
nightbuild clean -C out/Official
```

Remove the generated build state completely:

```text
nightbuild clobber -C out/Official
```

Verbose output:

```text
nightbuild build -C out/Official -v
```

## Design

NightBuild deliberately separates **generation** from **execution**.

### Generation

`nightbuild gen` reads the project's `BUILD.nb`, resolves targets and dependencies, loads toolchains, evaluates configuration, and produces a concrete command database.

The expensive decisions are made up front.

### Configuration

`nightbuild args` applies the project's `args.nb` configuration and regenerates the build graph.

This means configuration is explicit rather than being rediscovered during every build.

### Execution

`nightbuild build` operates on that concrete graph.

There is no need to repeatedly parse a high-level build language or rediscover build rules while compiling.

This gives NightBuild a design that is conceptually similar to GN's generation model while retaining the direct execution model that makes Ninja fast.

## Scheduling

NightBuild includes a dependency-aware parallel scheduler designed to prioritize work intelligently without turning scheduling into an expensive optimization problem.

Scheduling considers factors such as:

* historical command duration
* source size
* confidence in historical measurements
* dependency unlock potential
* resource pressure
* previously unseen commands

The scheduler is intended to make good decisions quickly rather than spend significant time trying to find a mathematically perfect schedule.

## Incremental Builds

NightBuild tracks build state using hashes rather than relying solely on file modification times.

That means a build can determine whether work actually needs to be repeated instead of assuming that a timestamp change necessarily means the contents changed.

Unchanged work is skipped.

## Benchmark

NightBuild is benchmarked against both **Ninja** and **samu** using the NightBuild project itself as the workload.

The current benchmark uses:

* 15 rounds
* clean builds
* randomized competitor order each round
* cache purging before every build
* the Ninja bundled with depot-tools
* a 2022 Apple M2 MacBook Air with 24 GB unified memory

Current results:

| Build system   |     Average |      Median |
| -------------- | ----------: | ----------: |
| **NightBuild** | **6.643 s** | **6.260 s** |
| Ninja          |     7.483 s |     7.090 s |
| samu           |     9.502 s |     9.200 s |

NightBuild was the fastest in the 15-round tournament.

That's a benchmark of one project on one machine, not a claim that NightBuild will beat every build system on every workload. The purpose is to measure real progress and keep performance regressions visible.

Those were the results from our benchmark script, [`tournament.sh`](tournament.sh).

## Bootstrap

NightBuild uses a small `Makefile` to bootstrap itself.

The intended lifecycle is:

```text
Makefile
   ↓
bootstrap NightBuild
   ↓
nightbuild gen
   ↓
nightbuild build
   ↓
optimized NightBuild
```

Make is used only to get the first NightBuild executable onto the machine. The canonical project description is `BUILD.nb`.

The full bootstrap process can additionally build an instrumented NightBuild, train its profile by compiling Ninja, merge the resulting LLVM profile data, and produce the optimized NightBuild executable.

## Building NightBuild

The simplest development workflow is:

```text
git clone https://github.com/Lord-Grumpypants/nightbuild.git
cd nightbuild
make
./nightbuild gen -C out/Official
./nightbuild build -C out/Official
```

After bootstrapping, NightBuild can build itself using its own build system.

For the full optimized bootstrap:

```text
python3 bootstrap.py
```

The bootstrap script installs any required Homebrew dependencies, builds the bootstrap NightBuild, trains PGO using Ninja, and produces the final optimized binary.

## Project Philosophy

NightBuild is intentionally **not** trying to become CMake.

The project favors:

* simple manifests
* predictable behavior
* fast execution
* small amounts of build-system machinery
* native implementation
* sensible defaults
* explicit dependency graphs

Advanced features should exist when they solve real problems, but they should not make a simple project feel complicated.

The ideal experience is that someone learning C++ can describe their project without first having to learn an entire build-system language.

## Status

NightBuild is an active project and is still evolving.

The current focus is making the core build experience extremely solid before adding layers of complexity.

The project is particularly interested in improving:

* build scheduling
* dependency handling
* generator support
* toolchain configuration
* incremental builds
* project/dependency management

## License

NightBuild is licensed under the **GNU General Public License v3.0 or later**.

See [`LICENSE`](LICENSE) for the full license text.
