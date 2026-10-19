# Contributing to NightBuild

Thanks for your interest in NightBuild!

NightBuild is a native C++23 build system focused on fast incremental builds, straightforward implementations, and keeping the build machinery simple.

Contributions of all sizes are welcome, including code, tests, documentation, bug reports, performance investigations, and thoughtful design discussions.

## Getting Started

We suggest keeping PRs **small**, so they're easier to review.

Before making larger changes, get familiar with the project, especially:

- `BUILD.nb`
- `args.nb` and `declare_args`
- Manifest semantics
- The generation and execution model
- The generated command database
- Dependency tracking and incremental build state

If you're unsure how something should work, open an issue to discuss it before implementing a substantial change.

## Good First Projects

There are plenty of ways to contribute without understanding the entire codebase.

### Documentation

- Fix unclear explanations.
- Add examples.
- Improve command documentation.
- Document `BUILD.nb` features.
- Document `args.nb` and `declare_args`.
- Explain architectural decisions.

### CLI and Diagnostics

- Clarify command descriptions.
- Improve error messages.
- Add missing examples.
- Make invalid command usage easier to understand.
- Improve target and dependency error messages.
- Improve verbose build output.

### Parser and Manifest Handling

- Improve parser diagnostics.
- Add useful validation.
- Support small, well-defined manifest conveniences.
- Add parser regression tests.
- Test invalid manifests and edge cases.
- Improve import and argument handling.

### Tests

- Add regression tests for existing features.
- Test invalid manifests.
- Test dependency handling.
- Test incremental builds.
- Test `args.nb` behavior.
- Test generator dependencies and outputs.
- Cover different target types and build configurations.

### Incremental Builds and Dependency Handling

- Find cases where unchanged work is rebuilt unnecessarily.
- Improve build-state validation.
- Add regression tests for incremental behavior.
- Improve dependency validation and diagnostics.
- Test complicated dependency graphs.
- Improve generated-file validation.

### Generator Support

- Add generator regression tests.
- Improve generator dependency handling.
- Improve generated-file validation.
- Investigate edge cases involving multiple outputs.

### Toolchains and Compile Commands

- Improve existing toolchain definitions.
- Add tests for toolchain loading.
- Improve compiler and linker diagnostics.
- Fix compile-command generation edge cases.
- Add coverage for different target types.
- Improve generated compile-command information.

### Scheduler and Performance

- Improve scheduling heuristics.
- Add scheduler tests.
- Improve historical timing data.
- Investigate scheduling regressions.
- Profile NightBuild.
- Reduce generation and scheduler overhead.
- Improve hashing performance.
- Benchmark changes against the existing tournament.

Performance improvements should be measured rather than assumed. Reproducible benchmarks and explanations of the results are especially valuable.

### macOS Support

- Test different target types.
- Improve framework and application handling.
- Test Objective-C and Objective-C++ builds.
- Improve Apple toolchain behavior.
- Improve filesystem and process-handling diagnostics.

### Bootstrap

- Improve dependency detection.
- Improve bootstrap diagnostics.
- Make the bootstrap more portable across supported macOS setups.
- Improve PGO training diagnostics.

### Examples

- Small executable projects.
- Static library projects.
- Multi-target projects.
- Projects using `args.nb`.
- Projects using generators.

## Looking Ahead: Linux Support

Linux support is a **future goal**, not a currently supported platform.

We'd welcome help investigating what it would take to bring NightBuild to Linux without compromising its design.

Potential contributions include:

- Identifying macOS-specific assumptions in the codebase.
- Documenting dependencies on Apple frameworks and system APIs.
- Investigating Linux compiler and linker toolchain requirements.
- Examining portability concerns in process management, filesystem operations, and terminal handling.
- Identifying platform-specific dependencies in the bootstrap.
- Proposing a minimal, maintainable approach to cross-platform support.

For now, portability investigations and design discussions are welcome. Please open an issue before beginning a substantial portability refactor so we can discuss the approach first.

There's no need to rush into a large cross-platform abstraction before understanding which differences actually need one.

## Interested in the Interesting Bits?

You don't have to start with a bug report or a pull request. Sometimes exploring an implementation is the most interesting part.

**If you enjoy statistics, mathematics, or optimization, take a look at `src/scheduler.cpp`.**

The scheduler is where dependency-aware parallel execution meets scheduling heuristics, historical timing data, and decisions about what work should run next. If you're interested in how to make scheduling decisions intelligently, there's plenty to explore.

**If you enjoy parsing, take a look at `src/toml.cpp`.**

Parser implementations have their own collection of interesting problems: recognizing syntax, handling edge cases, validating input, and producing useful diagnostics. If that kind of work is your thing, you might enjoy digging into it.

They're both quite cool in their own ways. Have a look around, follow the code, and see what you find!

## Before Opening a PR

Please make sure that:

- The project still bootstraps successfully.
- NightBuild can build itself.
- Relevant tests pass.
- New behavior has regression coverage where appropriate.
- The change does not introduce unnecessary dependencies.
- The change does not add unnecessary architectural complexity.

### Dependencies

NightBuild aims to remain lightweight, but that doesn't mean reinventing the wheel.

Prefer existing standard-library functionality when it provides a good solution. However, **don't build a homegrown replacement for an established library or tool just to avoid a dependency** when that library or tool would solve the problem substantially better.

Before introducing a third-party dependency, open an issue explaining:

- The problem it solves.
- Why existing standard-library functionality or the current implementation isn't sufficient.
- The alternatives considered.
- The benefits and trade-offs of the dependency.

Discuss the proposal before adding the dependency. The goal is to make sensible engineering decisions, not to prohibit dependencies categorically.

Please avoid unnecessary bloat and large architectural changes. Open an issue to discuss significant design changes before implementing them.

## Development

NightBuild is built with native C++23 and is intended to remain lightweight.

When working on the project, prefer:

- Small, focused changes.
- Straightforward implementations.
- Clear diagnostics over clever abstractions.
- Generation separate from execution.
- Explicit dependency graphs.
- Keeping `BUILD.nb` simple.
- Keeping `args.nb` focused on configuration.
- Keeping the generated command database concrete and deterministic.
- Existing standard-library functionality when it is a good fit.
- Established libraries and tools when they provide a substantially better solution than reinventing existing functionality.

Keep dependencies intentional and implementations maintainable. Simplicity is valuable, but not when it comes at the cost of needlessly rebuilding functionality that already exists.

## Areas of the Codebase

The main areas of NightBuild are intentionally separated:

- **CLI** — command-line parsing, commands, help, and user-facing diagnostics.
- **Manifest parser** — `BUILD.nb`, imports, expressions, targets, and `args.nb`.
- **Build graph** — targets, dependencies, generators, and actions.
- **Scheduler** — dependency-aware parallel execution.
- **Toolchains** — compiler, linker, framework, and package configuration.
- **Incremental state** — hashes, command state, and determining what needs rebuilding.
- **Compile commands** — generation of `compile_commands.json`.
- **TUI** — build progress and live status.
- **Filesystem watching** — filesystem event handling.
- **Tests** — regression coverage for the build system.

A good contribution should generally fit cleanly into one of these areas rather than introducing a new abstraction across the entire project.

## Design Principles

NightBuild deliberately keeps generation and execution separate.

- `nightbuild gen` should make the expensive build decisions up front.
- `nightbuild args` should configure those decisions without turning `args.nb` into a second build language.
- `nightbuild build` should execute the already-generated command graph efficiently.

When adding a feature, ask:

1. Does this solve a real build-system problem?
2. Does it make simple projects harder to understand?
3. Can it be implemented without adding significant machinery?
4. Does it belong in generation, configuration, or execution?
5. Can it be tested independently?
6. Are we reusing existing functionality where appropriate, or unnecessarily reinventing the wheel?

If a change requires significant architectural complexity, open an issue before implementing it.

## Get Involved

You don't need to arrive with a pull request ready.

Feedback on the architecture, bug reports, reproducible test cases, performance investigations, documentation improvements, and thoughtful design proposals are all valuable contributions.

If you're interested in working on NightBuild, check the open issues or open an issue describing your idea before starting a larger change.

**Constructive criticism is welcome.** If you think something could be simpler, faster, more reliable, or better designed, explain why. Good technical arguments are worth discussing.

The goal is to make NightBuild faster, simpler, and more reliable without compromising its design principles.

Thanks for taking the time to explore NightBuild!