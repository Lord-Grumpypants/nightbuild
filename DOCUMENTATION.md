# NightBuild Manifest Language

NightBuild uses `.nb` files to describe projects, targets, compiler settings, dependencies, and build configuration.

The manifest language is designed to keep project configuration concise and readable while giving NightBuild enough information to construct and execute a build graph.

Unlike Makefiles or CMake's configuration language, NightBuild manifests use a Python-like, indentation-based syntax with explicit target declarations and property assignments.

## Table of Contents

- [Getting Started](#getting-started)
- [Basic Syntax](#basic-syntax)
- [Projects](#projects)
- [Variables](#variables)
- [Values and Types](#values-and-types)
- [Lists](#lists)
- [Target Declarations](#target-declarations)
- [Target Types](#target-types)
- [Target Properties](#target-properties)
- [Package Action](#package-action)
- [Compiler Flags](#compiler-flags)
- [Conditional Configuration](#conditional-configuration)
- [Build Arguments](#build-arguments)
- [Importing Manifests](#importing-manifests)
- [Default Targets](#default-targets)
- [Custom Actions](#custom-actions)
- [Generators](#generators)
- [Comments](#comments)
- [Error Handling](#error-handling)
- [Complete Example](#complete-example)
- [Language Reference](#language-reference)

## Getting Started

A minimal NightBuild project can be described using a `BUILD.nb` file:

    project("HelloWorld")

    executable("hello"):
        sources = ["src/main.cpp"]

This declares a project named `HelloWorld` and an executable target named `hello` built from `src/main.cpp`.

Target declarations use a colon followed by an indented block. Properties inside that block describe how NightBuild should build the target.

A slightly more complete example:

    project("Example")

    default = ["hello"]

    executable("hello"):
        sources = ["src/main.cpp"]
        include_dirs = ["include"]
        cxxflags = ["-std=c++23", "-O2"]

The manifest describes the target without requiring the user to write individual compiler and linker commands.

## Basic Syntax

NightBuild manifests use indentation to define blocks.

### Indentation

Target declarations, conditional statements, and argument declarations introduce blocks using a colon.

    executable("app"):
        sources = ["src/main.cpp"]
        cxxflags = ["-std=c++23"]

Use spaces for indentation. Tabs are not permitted for indentation.

Statements at the same indentation level belong to the same block.

### Assignments

Use `=` to assign a value:

    sources = ["src/main.cpp"]

Use `+=` to append values to an existing list:

    sources += ["src/renderer.cpp"]

For target properties, `+=` appends to the property's existing values.

For variables, `+=` requires the variable to have already been defined as a list.

### Identifiers

Variable names and argument names must begin with a letter or underscore. Subsequent characters may contain letters, digits, or underscores.

Valid identifiers include:

    sources
    build_mode
    _internal
    optimization_level

Identifiers are case-sensitive.

## Projects

Use `project()` to declare the project name.

    project("NightBuildExample")

The argument must be a quoted string.

A project can also define its default targets:

    project("NightBuildExample")

    default = ["app"]

The `default` property accepts a string or a list of strings.

Using `+=` appends targets to the existing default-target list:

    default = ["app"]
    default += ["tests"]

Every default target must correspond to a declared target.

## Variables

Variables allow values to be reused throughout a manifest.

    common_sources = [
        "src/core.cpp",
        "src/config.cpp"
    ]

    common_flags = [
        "-std=c++23",
        "-Wall"
    ]

    executable("app"):
        sources = common_sources
        cxxflags = common_flags

Variable references use the variable name directly, without a prefix.

Variables must be defined before they are referenced.

### Appending to Variables

Use `+=` to append a value or another list:

    sources = ["src/main.cpp"]
    sources += ["src/renderer.cpp"]

    more_sources = ["src/platform.cpp"]
    sources += more_sources

The variable on the left side must already exist and contain a list.

Appending to an undefined variable or a non-list variable produces an error.

### Variable Scope

Variables are maintained by the manifest parser's shared configuration state. Imported manifests are parsed using the same variable state, allowing configuration values to be reused across files.

Variables are configuration values, not general-purpose programming variables. The language does not provide arbitrary function calls, loops, or general expression evaluation.

## Values and Types

The manifest language supports three value types:

| Type | Examples |
|---|---|
| String | `"src/main.cpp"` |
| Boolean | `true`, `false` |
| List | `["a.cpp", "b.cpp"]` |

### Strings

Strings must be enclosed in double quotes.

    project("MyProject")

Supported escape sequences include:

| Escape | Meaning |
|---|---|
| `\"` | Double quote |
| `\\` | Backslash |
| `\n` | Newline |
| `\t` | Tab |

Unsupported escape sequences produce an error.

### Booleans

Boolean values can be written as:

    enable_feature = true
    debug_build = false

The parser accepts both lowercase and initial-capital forms:

    enabled = true
    disabled = false

    enabled = True
    disabled = False

Boolean values are particularly useful in conditional configuration.

### Lists

Lists use square brackets and comma-separated values.

    sources = [
        "src/main.cpp",
        "src/renderer.cpp",
        "src/platform.cpp"
    ]

Lists can contain strings, booleans, or nested lists at the parser level. However, properties such as `sources`, `dependencies`, and `cxxflags` require strings or lists of strings.

A trailing comma is not required.

Multiline lists are supported:

    cxxflags = [
        "-std=c++23",
        "-O3",
        "-Wall"
    ]

## Target Declarations

Targets are the primary building blocks of a NightBuild project.

The general syntax is:

    target_type("target_name"):
        property = value

For example:

    executable("editor"):
        sources = ["src/main.cpp"]
        include_dirs = ["include"]

The declaration identifies the target type and name. Its indented block specifies its configuration.

Target names must be unique across the loaded project, including imported manifests.

### Multiple Targets

A project can declare multiple targets:

    project("Editor")

    static_library("core"):
        sources = [
            "src/core.cpp",
            "src/document.cpp"
        ]

    executable("editor"):
        sources = ["src/main.cpp"]
        dependencies = ["core"]

Targets are stored by name and validated after manifest loading.

## Target Types

NightBuild supports the following target declarations.

### Executable

Creates an executable target.

    executable("app"):
        sources = ["src/main.cpp"]

If no output is specified, the output defaults to the target name.

### App

Declares a macOS application target.

    app("MyApplication"):
        sources = ["src/main.mm"]

If no output is specified, the output defaults to `MyApplication.app`.

### Static Library

Declares a static library.

    static_library("core"):
        sources = [
            "src/core.cpp",
            "src/parser.cpp"
        ]

The alias `static_lib` is also supported.

If no output is specified, the default output is `libcore.a`.

### Shared Library

Declares a shared library.

    shared_library("engine"):
        sources = ["src/engine.cpp"]

The alias `shared_lib` is also supported.

If no output is specified, the default output is `libengine.dylib`.

### Object

Declares an object target.

    object("parser"):
        sources = ["src/parser.cpp"]

An object target must contain exactly one source file.

If no output is specified, the default output is `parser.o`.

### Framework

Declares a framework target.

    framework("Graphics"):
        sources = ["src/graphics.mm"]

If no output is specified, the default output is `Graphics.framework`.

### Generator

Declares a generator target.

    generator("generated_sources"):
        sources = ["tools/generate.cpp"]
        generator_deps = ["generator_support"]

Generator targets must have at least one source.

They cannot use ordinary `dependencies`. Use `generator_deps` to express dependencies on other generator targets.

The parser recognizes generator targets as a distinct target type, and project validation enforces their dependency rules.

### Action

Declares a custom action target.

    action("generate_version"):
        action = ["python3", "tools/version.py"]
        inputs = ["tools/version.py"]
        outputs = ["generated/version.h"]

The alias `action()` maps to the internal `ACTION` target type.

An action must specify an `action` command and either `output` or `outputs`.

See [Custom Actions](#custom-actions) for details.

## Target Properties

Target properties configure the target's inputs, dependencies, compiler settings, and outputs.

### Sources

Specifies source files.

    sources = ["src/main.cpp"]

Multiple source files can be listed:

    sources = [
        "src/main.cpp",
        "src/application.cpp"
    ]

The `+=` operator appends additional sources.

Non-action, non-generator targets must have at least one source.

### Dependencies

Specifies dependencies on other targets.

    dependencies = ["core", "utilities"]

The shorter alias `deps` is also supported:

    deps = ["core", "utilities"]

Dependencies must refer to existing targets.

A target cannot depend on itself. Generator targets cannot be used as ordinary dependencies; use `generator_deps` instead.

### Generator Dependencies

Specifies dependencies on generator targets.

    generator_deps = ["generate_headers"]

Every entry must refer to an existing target whose type is `generator`.

A target cannot use itself as a generator dependency.

### Include Directories

Specifies include directories.

    include_dirs = [
        "include",
        "third_party/include"
    ]

### C Compiler Flags

Specifies C compiler flags.

    cflags = [
        "-Wall",
        "-Wextra"
    ]

### C++ Compiler Flags

Specifies C++ compiler flags.

    cxxflags = [
        "-std=c++23",
        "-O3"
    ]

### Linker Flags

Specifies linker flags.

    ldflags = [
        "-Wl,-dead_strip"
    ]

### Toolchain Includes

Specifies JSON files that define compiler toolchains for a target. Toolchain definitions configure the compiler executable, source-file extension, compiler flags property, dependency-generation flags, and output-file extension.

    toolchain_include = [
        "toolchains/CXX.json",
        "toolchains/OBJCXX.json"
    ]

Each entry specifies a path to a JSON toolchain definition. These paths are consumed by the build system during toolchain resolution and build generation.

For example, `toolchains/CXX.json` can define a C++ compiler:

    {
        "name": "C++",
        "extension": ".cpp",
        "compiler": "clang++",
        "flags_key": "cxxflags",
        "dependency_flags": ["-MMD", "-MF"],
        "description": "CXX",
        "output_extension": ".o"
    }

The fields specify the toolchain's name, supported source extension, compiler executable, target property containing compiler flags, dependency-generation arguments, description, and compiled output extension.

The `dependency_flags` field accepts an array of compiler arguments. The legacy `dep_flag` string field is also supported for compatibility; new toolchain definitions should prefer `dependency_flags`.

Custom JSON toolchains allow projects to configure compiler behavior without modifying NightBuild's source code. The referenced compiler must be available in the build environment, and the target must provide the flag property specified by `flags_key`.

### Package Configuration

Specifies packages to resolve through `pkg-config`.

    pkg_config = [
        "notcurses",
        "libuv"
    ]

The manifest parser records these package names for downstream build processing.

### Library Directories

Specifies library search directories.

    library_dirs = [
        "lib",
        "third_party/lib"
    ]

### Libraries

Specifies libraries to link against.

    libraries = [
        "pthread",
        "z"
    ]

### Frameworks

Specifies frameworks.

    frameworks = [
        "AppKit",
        "Foundation",
        "QuartzCore"
    ]

### Output

Specifies a single output path.

    output = "out/my_app"

The value must be a string.

The `output` property does not support `+=`.

For action targets, either `output` or `outputs` must be supplied. If only `outputs` is supplied, the first output becomes the primary output.

### Inputs

Specifies inputs for an action target.

    inputs = [
        "schema.json",
        "tools/generate.py"
    ]

The parser stores these paths for downstream build processing.

### Outputs

Specifies outputs for an action target.

    outputs = [
        "generated/schema.h",
        "generated/schema.cpp"
    ]

At least one output is required for an action target, unless the singular `output` property is supplied.

### Package Action

Specifies a command to run as a packaging step for a target. This is useful when an application needs additional assets bundled after its normal build steps.

    app("NightNotes"):
        sources = ["src/main.mm"]
        package_action = ["npm", "run", "bundle"]

The value is a list of strings containing the executable and its arguments. Keep the executable and each argument as separate list elements rather than combining them into a single command string.

The optional `package_inputs` property lists additional files that affect
the packaging step. NightBuild tracks these files when deciding whether
packaging needs to run again.

    app("NightNotes"):
        sources = ["src/main.mm"]
        package_action = ["npm", "run", "bundle"]
        package_inputs = [
            "web/editor.js",
            "web/editor.css"
        ]

NightBuild reports this step as `PACKAGE` followed by the target name. The command is executed as part of building the target.

### Pre-Actions

Specifies lists of commands associated with a target's pre-actions.

    pre_actions = [
        ["python3", "tools/prepare.py"],
        ["python3", "tools/check.py"]
    ]

Each inner list represents a command and its arguments.

Additional commands can be appended:

    pre_actions += [
        ["python3", "tools/prepare_more.py"]
    ]

The parser stores these commands as structured argument lists. Their execution behavior is determined by the build system.

## Compiler Flags

NightBuild supports standard flag properties and additional flag properties.

The standard properties are:

    cflags = ["-Wall"]
    cxxflags = ["-std=c++23", "-O3"]
    ldflags = ["-Wl,-dead_strip"]

Additional compiler flag groups can be specified using identifiers ending in `flags`.

For example:

    objcflags = ["-fobjc-arc"]
    asmflags = ["-g"]

The flag property name must be a valid identifier.

Do not use the `flags.` prefix:

    cxxflags = ["-std=c++23"]

The parser explicitly rejects names such as `flags.cxxflags`.

### Flag Assignment and Appending

Replace a flag list:

    cxxflags = ["-std=c++23"]

Append to an existing flag list:

    cxxflags += ["-O3"]

For `cxxflags`, NightBuild synchronizes the value with its internal C++ compiler flag storage.

## Conditional Configuration

NightBuild supports `if` and `else` blocks at the project level and inside target declarations.

Conditions can evaluate booleans, compare values, and combine expressions.

### Boolean Conditions

    enable_tests = true

    if enable_tests:
        executable("tests"):
            sources = ["tests/main.cpp"]

A condition must evaluate to a boolean.

### Equality

Use `==` to compare values:

    build_mode = "release"

    if build_mode == "release":
        executable("app"):
            sources = ["src/main.cpp"]

Use `!=` for inequality:

    if build_mode != "debug":
        cxxflags = ["-O3"]

The supported comparison operators are `==` and `!=`.

### Logical Operators

Conditions support:

- `and`
- `or`
- `not`

Example:

    is_macos = true
    enable_tests = true

    if is_macos and enable_tests:
        executable("tests"):
            sources = ["tests/main.cpp"]

Logical operators can be combined:

    if is_macos and not enable_tests:
        executable("app"):
            sources = ["src/main.cpp"]

Parentheses can group expressions:

    if (is_macos and enable_tests):
        executable("tests"):
            sources = ["tests/main.cpp"]

The parser evaluates `not` before comparisons, comparisons before `and`, and `and` before `or`.

### Else Blocks

Use `else:` to specify an alternative branch.

    if build_mode == "debug":
        cxxflags = ["-O0", "-g"]
    else:
        cxxflags = ["-O3"]

The `else` block must immediately follow the corresponding `if` block at the same indentation level.

### Conditional Target Properties

Conditions can also be used inside targets.

    executable("app"):
        sources = ["src/main.cpp"]

        if build_mode == "debug":
            cxxflags = ["-O0", "-g"]
        else:
            cxxflags = ["-O3"]

Only the active branch is applied to the target configuration.

### Supported Condition Values

Conditions can evaluate:

- Boolean literals.
- Variables containing supported values.
- Equality and inequality comparisons.
- Logical expressions using `and`, `or`, and `not`.
- Parenthesized expressions.

Arithmetic expressions, function calls, loops, and arbitrary code execution are not supported by the condition evaluator.

## Build Arguments

Build arguments allow users to configure a project without editing its main manifest.

### Declaring Arguments

Use `declare_args()` to declare an argument and provide its default value.

    declare_args("build_mode"):
        default = "release"

Arguments can have string, boolean, or list defaults.

For example:

    declare_args("enable_tests"):
        default = true

    declare_args("extra_flags"):
        default = ["-Wall"]

### Overriding Arguments

NightBuild reads overrides from `args.nb` in the selected build directory.

For example, a build directory containing `args.nb` could define:

    build_mode = "debug"
    enable_tests = false
    extra_flags = ["-Wall", "-Wextra"]

These values override the corresponding declared defaults.

The argument names must have been declared in the loaded manifest configuration.

Unknown argument names produce an error.

The arguments file supports assignments using `=`. The `+=` operator is not supported in `args.nb`.

### Using Arguments

Declared arguments can be referenced like ordinary variables.

    declare_args("build_mode"):
        default = "release"

    executable("app"):
        sources = ["src/main.cpp"]

        if build_mode == "debug":
            cxxflags = ["-O0", "-g"]
        else:
            cxxflags = ["-O3"]

This allows the same manifest to describe different build configurations.

## Importing Manifests

The `import` statement loads another manifest file.

    import "build/common.nb"
    import "build/libraries.nb"

Imports use paths relative to the directory containing the importing manifest.

An imported file can define variables, targets, and additional imports.

### Organizing a Project

A project can separate shared configuration and targets into multiple files.

Example layout:

    BUILD.nb
    build/
        common.nb
        libraries.nb
    src/
        main.cpp
        core.cpp

Root manifest:

    project("Example")

    import "build/common.nb"
    import "build/libraries.nb"

    executable("app"):
        sources = ["src/main.cpp"]
        dependencies = ["core"]

Shared configuration:

    common_flags = [
        "-std=c++23",
        "-Wall"
    ]

Library manifest:

    static_library("core"):
        sources = ["src/core.cpp"]
        cxxflags = common_flags

### Import Resolution

NightBuild resolves imported paths relative to the importing manifest.

Imported files are canonicalized before loading. This allows the loader to identify repeated imports and detect import cycles.

A file already loaded into the project is not loaded a second time.

### Duplicate Targets

Target names must be unique across the complete imported project.

If two manifests declare the same target name, NightBuild reports a duplicate-target error.

### Import Cycles

Circular imports are rejected.

For example, if `BUILD.nb` imports `common.nb`, and `common.nb` imports `BUILD.nb`, NightBuild reports an import-cycle error.

### Configuration Tracking

NightBuild incorporates the paths and contents of loaded manifests into its configuration fingerprint.

Consequently, changes to imported manifest files can affect configuration tracking even when the root manifest itself has not changed.

## Default Targets

The `default` property determines which targets are considered default build targets.

    default = ["app"]

Multiple default targets can be declared:

    default = ["app"]
    default += ["tests"]

The property accepts either a single string or a list of strings.

Every default target must exist in the completed project. Unknown default targets cause validation to fail.

## Custom Actions

Action targets represent custom commands that produce declared outputs.

    action("generate_version"):
        action = [
            "python3",
            "tools/generate_version.py"
        ]
        inputs = ["tools/generate_version.py"]
        outputs = ["generated/version.h"]

The `action` property is a list of strings representing the command and its arguments.

The action target must specify a command and at least one output.

### Single Output

Use `output` when an action produces one primary output:

    action("generate_config"):
        action = [
            "python3",
            "tools/generate_config.py"
        ]
        inputs = ["config.json"]
        output = "generated/config.h"

### Multiple Outputs

Use `outputs` when an action produces multiple outputs:

    action("generate_parser"):
        action = [
            "python3",
            "tools/generate_parser.py"
        ]
        inputs = ["grammar.json"]
        outputs = [
            "generated/parser.cpp",
            "generated/parser.hpp"
        ]

When `outputs` is specified without `output`, the first entry becomes the primary output.

### Important Notes

The manifest parser validates action declarations and stores their commands, inputs, and outputs. Details such as command execution, dependency scheduling, and output freshness are handled by the build system.

Use action targets for custom build steps that do not fit the standard compilation and linking target types.

## Generators

Generator targets represent targets designated for generating build inputs.

    generator("generate_sources"):
        sources = ["tools/generate.cpp"]
        generator_deps = ["generator_support"]

A generator target must have at least one source.

Generator targets cannot declare ordinary target dependencies through `dependencies` or `deps`. Use `generator_deps` for dependencies on other generator targets.

Each generator dependency must exist and must itself be a generator target.

The parser recognizes generator targets separately from ordinary build targets, allowing NightBuild to apply generator-specific dependency rules.

## Comments

Use `#` to begin a comment outside a quoted string.

    # Project declaration
    project("Example")

    executable("app"):
        sources = ["src/main.cpp"]  # Main source file

A `#` character inside a quoted string is preserved.

    generated_file = "generated/file#1.hpp"

Comments are removed before the remaining statement is parsed.

## Error Handling

NightBuild validates manifest syntax and project structure and reports errors with the manifest path and line number where applicable.

Examples of errors include:

- Invalid indentation.
- Unsupported string escape sequences.
- Unknown variables.
- Missing assignment values.
- Invalid target declarations.
- Unknown target properties.
- Duplicate target names.
- Missing source files in target declarations.
- Invalid target dependency references.
- Generator dependency violations.
- Missing action commands or outputs.
- Unknown build arguments.
- Duplicate argument declarations.
- Invalid default targets.
- Import cycles.

For example, referencing an undefined variable:

    executable("app"):
        sources = source_files

If `source_files` has not been defined, parsing fails with an unknown-variable error.

Similarly, this declaration is invalid because the target has no source:

    executable("app"):
        cxxflags = ["-O3"]

A standard executable target must declare at least one source.

## Complete Example

The following example combines project configuration, arguments, imports, variables, conditions, target declarations, and dependencies.

    project("ExampleApp")

    import "build/common.nb"

    declare_args("build_mode"):
        default = "release"

    declare_args("enable_tests"):
        default = true

    default = ["app"]

    app_sources = [
        "src/main.mm",
        "src/application.mm"
    ]

    executable("core"):
        sources = [
            "src/core.cpp",
            "src/document.cpp"
        ]
        include_dirs = ["include"]
        cxxflags = common_flags

        if build_mode == "debug":
            cxxflags += ["-O0", "-g"]
        else:
            cxxflags += ["-O3"]

    executable("app"):
        sources = app_sources
        dependencies = ["core"]
        include_dirs = ["include"]
        cxxflags = common_flags

    if enable_tests:
        executable("tests"):
            sources = ["tests/main.cpp"]
            dependencies = ["core"]
            cxxflags = common_flags

A corresponding `build/common.nb` could contain:

    common_flags = [
        "-std=c++23",
        "-Wall",
        "-Wextra"
    ]

An `args.nb` file in the selected build directory could override the defaults:

    build_mode = "debug"
    enable_tests = false

This keeps shared configuration reusable while allowing build-specific settings to be changed without modifying the main manifest.

## Language Reference

### Project Statements

| Syntax | Description |
|---|---|
| `project("name")` | Declares the project name. |
| `default = ["target"]` | Sets default targets. |
| `default += ["target"]` | Appends default targets. |
| `import "path"` | Imports another manifest. |
| `declare_args("name"):` | Declares a configurable argument. |

### Target Declarations

| Declaration | Target type |
|---|---|
| `executable("name"):` | Executable |
| `app("name"):` | macOS application |
| `static_library("name"):` | Static library |
| `static_lib("name"):` | Static library alias |
| `shared_library("name"):` | Shared library |
| `shared_lib("name"):` | Shared library alias |
| `object("name"):` | Object file |
| `framework("name"):` | Framework |
| `generator("name"):` | Generator |
| `action("name"):` | Custom action |

### Supported Properties

| Property | Value type | Purpose |
|---|---|---|
| `sources` | String or list of strings | Source files |
| `dependencies` | String or list of strings | Target dependencies |
| `deps` | String or list of strings | Dependency alias |
| `generator_deps` | String or list of strings | Generator dependencies |
| `include_dirs` | String or list of strings | Include directories |
| `cflags` | String or list of strings | C compiler flags |
| `cxxflags` | String or list of strings | C++ compiler flags |
| `ldflags` | String or list of strings | Linker flags |
| `toolchain_include` | String or list of strings | Toolchain configuration paths |
| `pkg_config` | String or list of strings | Package configuration dependencies |
| `library_dirs` | String or list of strings | Library search directories |
| `libraries` | String or list of strings | Libraries |
| `frameworks` | String or list of strings | Frameworks |
| `action` | String or list of strings | Custom action command |
| `inputs` | String or list of strings | Action inputs |
| `outputs` | String or list of strings | Action outputs |
| `output` | String | Primary output |
| `pre_actions` | List of lists of strings | Pre-action commands |
| `package_action` | List of strings | Packaging command for a target |
| `package_inputs` | String or list of strings | Additional packaging inputs |
| `*_flags` | String or list of strings | Additional compiler flag groups |

### Operators

| Operator | Purpose |
|---|---|
| `=` | Assigns or replaces a value. |
| `+=` | Appends to an existing list or target property. |
| `==` | Equality comparison. |
| `!=` | Inequality comparison. |
| `and` | Logical conjunction. |
| `or` | Logical disjunction. |
| `not` | Logical negation. |

### Language Limitations

The manifest language is intentionally limited to configuration rather than general-purpose programming.

The parser does not implement:

- `for` or `while` loops.
- Arbitrary function definitions or calls.
- Arithmetic expression evaluation.
- General-purpose code execution.
- Attribute access or object-oriented expressions.
- Conditional operators beyond the documented comparison and logical operators.

This keeps the manifest focused on describing a build rather than implementing the build logic itself.

---

**NightBuild** — a native C++23 build system for macOS.