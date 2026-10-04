# NightBuild Documentation

NightBuild is a native C++ build system designed around **simple build descriptions, explicit toolchains, generated command databases, dependency-aware scheduling, and fast incremental builds**.

NightBuild uses a small dedicated build description language named:

```text
BUILD.nb
```

The build description describes the project and its targets.

NightBuild then generates a concrete command database and executes that database with its scheduler.

The basic architecture is:

```text
BUILD.nb
    │
    ▼
  Parser
    │
    ▼
Project + Targets
    │
    ▼
Dependency Graph
    │
    ▼
Command Database
    │
    ▼
  Scheduler
    │
    ▼
Parallel Build
```

NightBuild is designed to keep the common build path simple. Advanced facilities such as includes, actions, and generator dependencies are available for larger projects without making them necessary for ordinary builds.

---

# 1. Basic Project Structure

A simple NightBuild project can look like:

```text
project/

├── BUILD.nb
├── src/
│   ├── main.cpp
│   └── other.cpp
├── include/
│   └── other.h
└── toolchains/
    ├── CXX.json
    └── OBJCXX.json
```

The project description is:

```text
BUILD.nb
```

Generate the build directory with:

```text
nightbuild gen -C build
```

Build the default targets with:

```text
nightbuild build -C build
```

Build a specific target with:

```text
nightbuild build -C build TARGET
```

Build multiple targets with:

```text
nightbuild build -C build TARGET1 TARGET2
```

---

# 2. BUILD.nb

`BUILD.nb` is NightBuild's native build description language.

Unlike a general configuration format, `BUILD.nb` is designed specifically around NightBuild's project and target model.

A minimal build description looks like:

```text
project("My Application")

default = [
    "myapp",
]

executable("myapp"):
    sources = [
        "src/main.cpp",
    ]
```

The basic syntax consists of:

```text
project(...)
```

for the project declaration,

```text
default = [...]
```

for the default target list,

and target declarations such as:

```text
executable("name"):
```

followed by indented target properties.

---

# 3. Project Declaration

A NightBuild project begins with a project declaration:

```text
project("NightBuild")
```

The string identifies the project.

For example:

```text
project("My Application")
```

The project declaration establishes the project represented by the `BUILD.nb` file.

---

# 4. Default Targets

A project can explicitly define which targets are built when no target names are supplied.

Use:

```text
default = [
    "nightbuild",
    "test",
]
```

For example:

```text
project("NightBuild")

default = [
    "nightbuild",
    "test",
]
```

Then:

```text
nightbuild build -C build
```

selects the targets listed by `default`.

This makes the project's normal build set explicit.

A project does not have to build every declared target by default.

For example:

```text
project("Example")

default = [
    "app",
]
```

can declare additional targets without automatically building them.

---

# 5. Targets

Targets are the fundamental build units in NightBuild.

A target is declared using a target constructor:

```text
executable("myapp"):
```

Properties belonging to that target are indented underneath it:

```text
executable("myapp"):
    sources = [
        "src/main.cpp",
    ]
```

The target name is the string passed to the target constructor.

For:

```text
executable("test"):
```

the target name is:

```text
test
```

and it can be selected with:

```text
nightbuild build -C build test
```

---

# 6. Target Types

NightBuild supports several target types.

The core target types include:

```text
executable
app
static_lib
shared_lib
object
framework
```

NightBuild also supports specialized build operations for advanced projects, including action and generator-related functionality.

The ordinary compilation path generally consists of:

```text
sources
    ↓
compile
    ↓
link
```

Advanced target types and operations extend this model when a project requires them.

---

# 7. Executable Targets

An executable target is declared with:

```text
executable("name"):
```

For example:

```text
executable("myapp"):
    sources = [
        "src/main.cpp",
        "src/app.cpp",
    ]
```

NightBuild compiles the source files and produces an executable.

A complete minimal project is therefore:

```text
project("My Application")

default = [
    "myapp",
]

executable("myapp"):
    sources = [
        "src/main.cpp",
    ]
```

---

# 8. Multiple Targets

A single `BUILD.nb` file can contain multiple targets.

For example:

```text
project("NightBuild")

default = [
    "nightbuild",
    "test",
]

executable("nightbuild"):
    sources = [
        "src/main.cpp",
        "src/cli.cpp",
        "src/scheduler.cpp",
    ]

executable("test"):
    sources = [
        "tests/main.cpp",
    ]
```

The generated command database contains operations for both targets.

The targets can then be selected independently:

```text
nightbuild build -C build nightbuild
```

or:

```text
nightbuild build -C build test
```

---

# 9. Target Selection

NightBuild supports explicit target selection from the command line.

Build the project's default targets:

```text
nightbuild build -C build
```

Build one target:

```text
nightbuild build -C build test
```

Build multiple targets:

```text
nightbuild build -C build nightbuild test
```

Target selection operates on the generated build graph.

If a selected target has dependencies, the required dependencies are included in the selected build.

---

# 10. Sources

Sources are declared with the `sources` property:

```text
executable("myapp"):
    sources = [
        "src/main.cpp",
        "src/app.cpp",
    ]
```

Multiple source files can be supplied:

```text
sources = [
    "src/main.cpp",
    "src/foo.cpp",
    "src/bar.cpp",
]
```

NightBuild determines how each source is compiled from its file extension and available toolchains.

For example:

```text
.cpp → CXX
.mm  → OBJCXX
```

A target can therefore contain multiple source languages when the corresponding toolchains are available.

---

# 11. Include Directories

Include directories are specified with:

```text
include_dirs = [
    "include",
]
```

For example:

```text
executable("myapp"):
    sources = [
        "src/main.cpp",
    ]

    include_dirs = [
        "include",
    ]
```

NightBuild converts the configured directories into compiler include options.

A source can then include a header from that directory:

```text
#include "app.h"
```

Include directories belong to the target that uses them.

---

# 12. Compiler Flags

Compiler flags are specified with:

```text
cflags = [
    "-std=c++23",
    "-O3",
    "-march=native",
]
```

For example:

```text
executable("myapp"):
    cflags = [
        "-std=c++23",
        "-O3",
        "-march=native",
    ]
```

These flags become part of the generated compilation commands.

NightBuild can therefore detect when the effective compilation command changes and invalidate the appropriate build operations.

---

# 13. Linker Flags

Linker-specific flags are specified with:

```text
ldflags = [
    "-flto=full",
]
```

For example:

```text
executable("myapp"):
    ldflags = [
        "-flto=full",
    ]
```

These flags are applied to the generated link command.

Compiler flags and linker flags are separate because they affect different build operations.

---

# 14. pkg-config

External packages can be specified with:

```text
pkg_config = [
    "notcurses",
]
```

For example:

```text
executable("nightbuild"):
    pkg_config = [
        "notcurses",
    ]
```

NightBuild incorporates the resulting compiler and linker configuration into the target's generated commands.

An empty list is valid:

```text
pkg_config = []
```

---

# 15. Library Directories

Library search directories are specified with:

```text
library_dirs = [
    "lib",
]
```

Multiple directories can be supplied:

```text
library_dirs = [
    "lib",
    "third_party/lib",
]
```

The directories are used when generating the target's link command.

An empty list is valid:

```text
library_dirs = []
```

---

# 16. Libraries

Libraries are specified without the `-l` prefix.

For example:

```text
libraries = [
    "foo",
    "bar",
]
```

NightBuild generates the corresponding linker arguments:

```text
-lfoo
-lbar
```

An empty list is valid:

```text
libraries = []
```

---

# 17. Apple Frameworks

macOS frameworks are specified with:

```text
frameworks = [
    "Foundation",
    "AppKit",
]
```

NightBuild generates the corresponding linker arguments:

```text
-framework Foundation
-framework AppKit
```

Frameworks are particularly useful for macOS application targets and Objective-C or Objective-C++ sources.

---

# 18. Toolchain Includes

A target can include one or more external toolchain definitions.

For example:

```text
toolchain_include = [
    "toolchains/OBJCXX.json",
    "toolchains/CXX.json",
]
```

Toolchain files describe how NightBuild should compile particular source types.

A toolchain can specify properties such as:

```text
name
extension
compiler
flags_key
dependency flags
description
output extension
```

For example:

```json
{
    "name": "Objective-C++",
    "extension": ".mm",
    "compiler": "clang++",
    "flags_key": "objcxxflags",
    "description": "OBJCXX",
    "output_extension": ".o"
}
```

---

# 19. Toolchain Selection

When NightBuild encounters a source file, it uses the source extension to select an appropriate toolchain.

For example:

```text
src/main.cpp
      │
      ▼
     .cpp
      │
      ▼
     CXX
      │
      ▼
   clang++
```

and:

```text
src/main.mm
      │
      ▼
     .mm
      │
      ▼
    OBJCXX
      │
      ▼
   clang++
```

This allows a target to combine different source types without manually specifying a compiler for every file.

---

# 20. Dependencies

Targets can depend on other targets.

For example:

```text
static_lib("library"):
    sources = [
        "src/library.cpp",
    ]

executable("app"):
    sources = [
        "src/main.cpp",
    ]

    deps = [
        "library",
    ]
```

The dependency relationship is:

```text
app
 │
 ▼
library
```

NightBuild ensures that the required dependency is available before the dependent target reaches its final build operation.

---

# 21. Dependency Graph

NightBuild resolves target dependencies during generation.

For example:

```text
app
 │
 ├── library
 │    │
 │    └── base
 │
 └── other
```

produces a dependency graph that determines the valid execution order.

Independent portions of the graph can still execute in parallel.

For example:

```text
        app
       /   \
 library   other
    │
   base
```

allows `other` and `library` to progress independently when their own dependencies are ready.

---

# 22. Dependency Validation

NightBuild validates the target graph during generation.

Unknown dependencies are errors.

Circular dependencies are errors.

For example:

```text
A → B
B → A
```

forms a cycle and cannot be resolved into a valid build order.

A valid graph must provide an ordering in which every target's dependencies can become available.

---

# 23. Generation

NightBuild separates generation from execution.

Generation is performed with:

```text
nightbuild gen -C build
```

The generator reads:

```text
BUILD.nb
```

and produces the concrete build representation.

Conceptually:

```text
BUILD.nb
    │
    ▼
  Parser
    │
    ▼
Project + Targets
    │
    ▼
Dependency Resolution
    │
    ▼
Command Generation
    │
    ▼
build/commands
```

Generation does not perform the normal compilation itself.

---

# 24. Command Database

NightBuild stores generated build operations in a command database.

The command database contains concrete build operations produced during generation.

Conceptually:

```text
BUILD.nb
    │
    ▼
Generation
    │
    ▼
build/commands
    │
    ▼
Scheduler
```

The execution phase can therefore work from concrete commands rather than repeatedly interpreting the high-level project description.

Command records can represent operations such as:

```text
PRE
CXX
OBJECT
LINK
ACTION
```

depending on the project.

---

# 25. Scheduler

The NightBuild scheduler executes the generated command graph.

The scheduler tracks:

* command dependencies
* ready commands
* completed commands
* command priorities
* estimated work
* resource pressure
* newly unlocked work

Conceptually:

```text
Command Database
       │
       ▼
    Scheduler
       │
       ├── ready commands
       ├── dependency state
       ├── priority
       ├── estimates
       └── resource pressure
       │
       ▼
Parallel execution
```

The scheduler is designed around the common compilation workload and attempts to keep useful work running whenever dependencies and system resources allow it.

---

# 26. Parallel Builds

Independent commands can run concurrently.

For example:

```text
main.cpp
foo.cpp
bar.cpp
scheduler.cpp
```

can be compiled independently:

```text
CXX main.cpp
CXX foo.cpp
CXX bar.cpp
CXX scheduler.cpp
```

Once the required object files are available, the link command becomes ready:

```text
CXX ─┐
CXX ─┤
CXX ─┼──→ LINK
CXX ─┘
```

NightBuild's scheduler determines when commands become executable.

---

# 27. Incremental Builds

NightBuild performs incremental builds using generated commands and build state.

When a build is repeated, NightBuild determines which operations still need to execute.

An up-to-date build can produce:

```text
nightbuild: no work to do.
```

Only the necessary operations are executed when something changes.

---

# 28. Hash-Based Build State

NightBuild uses content-based hashing as an important part of incremental build decisions.

A file's modification timestamp changing does not necessarily mean that its contents changed.

Conceptually:

```text
timestamp changed
        ≠
content changed
```

This allows operations to remain valid when a file has merely been touched without changing its contents.

Command configuration can also participate in build identity.

Therefore a change to compiler flags, linker flags, or other command inputs can cause the affected operation to be rebuilt.

---

# 29. Missing Outputs

If an expected output is missing, NightBuild can determine which operation must recreate it.

For example:

```text
rm -rf test
```

followed by:

```text
nightbuild build -C build test
```

causes the necessary build operations to run again.

A missing output does not require unrelated targets to be rebuilt.

---

# 30. Command Changes

Build commands are part of the generated build state.

For example, changing:

```text
-O2
```

to:

```text
-O3
```

changes the compilation command.

The build system can therefore distinguish:

```text
same source
+
same command
```

from:

```text
same source
+
different command
```

The latter requires the affected operation to be rebuilt.

---

# 31. Rebuild

NightBuild provides an explicit rebuild operation:

```text
nightbuild rebuild -C build
```

Rebuild is useful when a developer intentionally wants the project's build outputs to be regenerated rather than relying entirely on normal incremental state.

---

# 32. Clean

The `clean` command removes generated build artifacts and state while preserving the source project.

Use:

```text
nightbuild clean -C build
```

The source tree and `BUILD.nb` remain intact.

---

# 33. Clobber

The `clobber` command removes the generated build directory and its associated generated build data.

Use:

```text
nightbuild clobber -C build
```

This is the strongest normal cleanup operation.

The project source tree remains separate from generated build state.

---

# 34. Verbose Builds

Normal NightBuild output is designed to remain concise.

Verbose mode can be enabled with:

```text
nightbuild build -C build --verbose
```

Verbose mode is useful when debugging generated commands, compiler arguments, linker arguments, or build behavior.

---

# 35. Build Output

NightBuild uses compact progress information during normal builds.

A build can display progress conceptually like:

```text
NightBuild · 3/8 · 4 jobs · CXX src/scheduler.cpp
```

and:

```text
NightBuild · 8/8 · ✓ LINK nightbuild
```

The goal is to provide useful information without filling the terminal with unnecessary command output.

---

# 36. Objective-C++ macOS Application

A macOS application can combine C++ and Objective-C++ sources.

For example:

```text
project("Example App")

default = [
    "app",
]

executable("app"):
    toolchain_include = [
        "toolchains/OBJCXX.json",
        "toolchains/CXX.json",
    ]

    sources = [
        "src/main.mm",
        "src/app.cpp",
    ]

    include_dirs = [
        "include",
    ]

    cflags = [
        "-std=c++23",
        "-O3",
        "-march=native",
    ]

    frameworks = [
        "Foundation",
    ]
```

The build can conceptually become:

```text
OBJCXX src/main.mm
CXX    src/app.cpp
LINK   app
```

---

# 37. PGO Builds

NightBuild can represent profile-guided builds directly as separate targets.

A typical arrangement contains:

```text
nightbuild
nightbuild-pgo
```

The normal target consumes a profile:

```text
-fprofile-instr-use=nightbuild.profdata
```

while the instrumented target produces profile data:

```text
-fprofile-instr-generate
```

Conceptually:

```text
nightbuild-pgo
      │
      ▼
instrumented executable
      │
      ▼
profile data
      │
      ▼
nightbuild.profdata
      │
      ▼
nightbuild
      │
      ▼
optimized executable
```

This allows NightBuild's own build description to describe its PGO workflow.

---

# 38. Example PGO Targets

A PGO-enabled project can contain a normal target:

```text
executable("nightbuild"):
    cflags = [
        "-O3",
        "-flto=full",
        "-fprofile-instr-use=nightbuild.profdata",
    ]

    ldflags = [
        "-flto=full",
        "-fprofile-instr-use=nightbuild.profdata",
    ]
```

and an instrumented target:

```text
executable("nightbuild-pgo"):
    cflags = [
        "-O3",
        "-flto=full",
        "-fprofile-instr-generate",
    ]

    ldflags = [
        "-fprofile-instr-generate",
    ]
```

The instrumented executable can be used to collect representative profile data for the normal NightBuild executable.

---

# 39. Advanced Features

NightBuild provides additional mechanisms for projects whose build graphs become more complicated.

These include:

```text
includes
actions
generator dependencies
generated sources
specialized build operations
```

These features are primarily useful for larger projects.

A small project normally does not need them.

The core NightBuild path remains:

```text
BUILD.nb
    ↓
targets
    ↓
sources
    ↓
compile
    ↓
link
```

---

# 40. Includes

Includes allow a large build description to be organized across multiple build-description files.

This becomes useful when a project contains many targets or distinct project components.

Conceptually:

```text
BUILD.nb
    │
    ├── core targets
    │
    ├── application targets
    │
    └── included descriptions
             │
             ├── test targets
             └── tool targets
```

The included definitions are incorporated into the project's build graph during generation.

Includes are primarily an organizational mechanism for larger projects.

---

# 41. Actions

Actions represent build operations that are not ordinary source compilation or final linking.

They can be useful for operations such as:

```text
resource processing
custom preprocessing
file generation
special build steps
```

Actions extend the command graph when a project needs operations outside the standard compile/link pipeline.

They are not required for ordinary C++ compilation.

---

# 42. Generator Dependencies

Some large projects need to build a program and then execute that program during the build.

For example:

```text
generator program
       │
       ▼
generated source
       │
       ▼
compiler
```

A generator dependency expresses this relationship.

The generator must first be built.

It can then run and produce files required by later build operations.

This is useful for:

* generated source code
* schema compilers
* protocol compilers
* resource generators
* other build-time programs

Generator dependencies are an advanced feature intended for projects that actually require generated build inputs.

---

# 43. Large Project Graphs

A large project may contain a graph such as:

```text
application
    │
    ├── libraries
    │
    ├── generated sources
    │
    ├── resource generators
    │
    ├── tests
    │
    └── build utilities
```

NightBuild's advanced graph mechanisms allow these relationships to be represented explicitly.

The important design principle is that large-project functionality should **scale the build system without making the basic build model unnecessarily complicated**.

---

# 44. Core Versus Advanced Features

NightBuild can be divided conceptually into a core build path and advanced scaling features.

## Core

```text
BUILD.nb
    ↓
project
    ↓
targets
    ↓
sources
    ↓
toolchains
    ↓
compile
    ↓
link
    ↓
incremental build
```

## Advanced

```text
includes
actions
generator dependencies
generated sources
complex dependency graphs
```

The advanced features exist for projects that need them.

They do not define the normal NightBuild workflow.

---

# 45. Complete NightBuild Example

The following represents the structure of a real NightBuild project:

```text
project("NightBuild")

default = [
    "nightbuild",
    "test",
]

executable("nightbuild"):
    toolchain_include = [
        "toolchains/OBJCXX.json",
        "toolchains/CXX.json",
    ]

    sources = [
        "src/main.cpp",
        "src/cli.cpp",
        "src/toml.cpp",
        "src/tui.cpp",
        "src/scheduler.cpp",
        "src/fsevents.cpp",
        "src/compile_commands.cpp",
    ]

    include_dirs = [
        "include",
    ]

    cflags = [
        "-std=c++23",
        "-O3",
        "-march=native",
        "-mtune=native",
        "-flto=full",
        "-fstrict-aliasing",
        "-fomit-frame-pointer",
        "-fwhole-program-vtables",
        "-fvirtual-function-elimination",
        "-fstrict-vtable-pointers",
        "-fprofile-instr-use=nightbuild.profdata",
        "-Wno-profile-instr-unprofiled",
    ]

    pkg_config = [
        "notcurses",
    ]

    ldflags = [
        "-flto=full",
        "-fuse-ld=/opt/homebrew/opt/lld/bin/ld64.lld",
        "-fprofile-instr-use=nightbuild.profdata",
        "-Wl,--icf=safe",
    ]

    library_dirs = []

    libraries = []

    frameworks = [
        "CoreServices",
    ]

executable("nightbuild-pgo"):
    toolchain_include = [
        "toolchains/CXX.json",
    ]

    sources = [
        "src/main.cpp",
        "src/cli.cpp",
        "src/toml.cpp",
        "src/tui.cpp",
        "src/scheduler.cpp",
        "src/fsevents.cpp",
        "src/compile_commands.cpp",
    ]

    include_dirs = [
        "include",
    ]

    cflags = [
        "-std=c++23",
        "-O3",
        "-march=native",
        "-mtune=native",
        "-flto=full",
        "-fstrict-aliasing",
        "-fomit-frame-pointer",
        "-fprofile-instr-generate",
    ]

    pkg_config = [
        "notcurses",
    ]

    ldflags = [
        "-fprofile-instr-generate",
        "-fuse-ld=/opt/homebrew/opt/lld/bin/ld64.lld",
    ]

    library_dirs = [
        "/opt/homebrew/opt/llvm/lib/clang/23/lib/darwin",
    ]

    libraries = [
        "clang_rt.profile_osx",
    ]

    frameworks = [
        "CoreServices",
    ]

executable("test"):
    sources = [
        "tests/main.cpp",
    ]
```

This demonstrates the major parts of the current NightBuild model:

```text
project
default targets
executable targets
toolchain includes
sources
include directories
compiler flags
pkg-config
linker flags
library directories
libraries
frameworks
PGO instrumentation
PGO profile use
test targets
```

---

# 46. Typical Workflow

A normal NightBuild workflow is:

```text
Edit BUILD.nb
      │
      ▼
Generate
      │
      ▼
Build
      │
      ▼
Modify source
      │
      ▼
Incremental build
```

Generate the project:

```text
nightbuild gen -C build
```

Build the default targets:

```text
nightbuild build -C build
```

After modifying a source file:

```text
nightbuild build -C build
```

NightBuild determines which commands actually need to execute.

---

# 47. Target-Specific Workflow

A project with several targets can select only the desired target.

For example:

```text
nightbuild build -C build test
```

builds the `test` target and its required dependencies.

Similarly:

```text
nightbuild build -C build nightbuild
```

builds the NightBuild executable.

Multiple targets can be requested:

```text
nightbuild build -C build nightbuild test
```

---

# 48. Generation and Execution

NightBuild intentionally separates:

```text
generation
```

from:

```text
execution
```

Generation answers:

> What commands exist, and how are they connected?

Execution answers:

> Which of those commands actually need to run, and when?

This produces a clean boundary:

```text
BUILD.nb
    │
    ▼
Generation
    │
    ▼
Concrete command database
    │
    ▼
Execution
    │
    ▼
Scheduler
```

---

# 49. Design Principles

NightBuild follows several principles.

## Keep the common path small

A normal project should be able to describe:

```text
project
target
sources
toolchains
flags
```

without requiring advanced build machinery.

## Generate concrete commands

The build phase should execute a concrete command graph rather than repeatedly interpreting the high-level project description.

## Make dependencies explicit

Target dependencies should be represented directly in the graph.

## Prefer content-based invalidation

Build decisions should depend on relevant content and command identity rather than timestamps alone.

## Exploit parallelism

Independent build operations should execute concurrently whenever possible.

## Scale without forcing complexity

Includes, actions, generators, and more complicated dependency mechanisms should be available when a large project needs them without burdening small projects.

## Keep BUILD.nb readable

The build description should describe the structure of the project rather than becoming a general-purpose programming language.

---

# 50. NightBuild Architecture

The complete NightBuild architecture can be summarized as:

```text
                         BUILD.nb
                            │
                            ▼
                          Parser
                            │
                            ▼
                   Project + Target Model
                            │
                 ┌──────────┼──────────┐
                 │          │          │
              Sources   Toolchains  Dependencies
                 │          │          │
                 └──────────┼──────────┘
                            │
                            ▼
                        Generation
                            │
                            ▼
                    Command Database
                            │
                            ▼
                         Scheduler
                            │
             ┌──────────────┼──────────────┐
             │              │              │
            CXX            CXX            CXX
             │              │              │
             └──────────────┼──────────────┘
                            │
                            ▼
                           LINK
                            │
                            ▼
                          Output
```

---

# 51. Summary

NightBuild's fundamental workflow is:

```text
BUILD.nb
    ↓
Project
    ↓
Targets
    ↓
Dependencies + Toolchains
    ↓
Generated Commands
    ↓
Command Database
    ↓
Scheduler
    ↓
Parallel Build
    ↓
Incremental Output
```

The core project description is intentionally compact:

```text
project(...)
default = [...]

executable("name"):
    ...
```

Targets describe independently selectable build units.

Sources describe what gets compiled.

Toolchains describe how source files are compiled.

Flags, libraries, frameworks, and packages describe the compilation and linking environment.

Generation converts the high-level build description into concrete operations.

The command database provides the boundary between generation and execution.

The scheduler executes those operations while respecting dependencies and available resources.

Advanced features such as includes, actions, and generator dependencies provide the additional graph machinery required by larger projects.

The central goal of NightBuild is therefore:

> **Describe the build simply. Generate it explicitly. Schedule concrete commands efficiently. Rebuild only what actually needs to change.**
