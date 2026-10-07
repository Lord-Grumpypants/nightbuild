## Contributing

We suggest you keep PRs **SMALL**, so review is easy.

We also suggest that you get familiar with the project, especially `BUILD.nb`, `args.nb`, manifest semantics, and the generation/execution model before making larger changes.

### Good First Projects

If you're new to NightBuild, these are good places to start:

* **Improve documentation**

  * Fix unclear explanations
  * Add examples
  * Improve command documentation
  * Document `BUILD.nb` features
  * Document `args.nb` and `declare_args`

* **Improve CLI help**

  * Clarify command descriptions
  * Improve error messages
  * Add missing examples
  * Make invalid command usage easier to understand

* **Add small parser features**

  * Improve diagnostics
  * Add useful validation
  * Support small, well-defined manifest conveniences
  * Add parser tests

* **Add tests**

  * Add regression tests for existing features
  * Test invalid manifests
  * Test dependency handling
  * Test incremental builds
  * Test `args.nb` behavior
  * Test generator dependencies

* **Improve build diagnostics**

  * Make failures easier to understand
  * Improve target and dependency error messages
  * Improve verbose build output

* **Improve incremental builds**

  * Find cases where unchanged work is rebuilt unnecessarily
  * Improve build-state validation
  * Add regression tests for incremental behavior

* **Improve dependency handling**

  * Add dependency validation
  * Improve dependency diagnostics
  * Add tests for more complicated dependency graphs

* **Improve generator support**

  * Add generator regression tests
  * Improve generator dependency handling
  * Improve generated-file validation

* **Improve toolchain support**

  * Improve existing toolchain definitions
  * Add tests for toolchain loading
  * Improve compiler and linker diagnostics

* **Improve compile command generation**

  * Fix edge cases
  * Add coverage for different target types
  * Improve generated compile command information

* **Improve the scheduler**

  * Improve scheduling heuristics
  * Add scheduler tests
  * Improve historical timing data
  * Investigate scheduling regressions

* **Improve performance**

  * Profile NightBuild
  * Reduce generation overhead
  * Reduce scheduler overhead
  * Improve hashing performance
  * Benchmark changes against the existing tournament

* **Improve macOS support**

  * Test different target types
  * Improve framework and application handling
  * Test Objective-C and Objective-C++ builds
  * Improve Apple toolchain behavior

* **Improve the bootstrap**

  * Improve dependency detection
  * Improve bootstrap diagnostics
  * Make the bootstrap more portable across supported macOS setups
  * Improve PGO training diagnostics

* **Add examples**

  * Small executable projects
  * Static library projects
  * Multi-target projects
  * Projects using `args.nb`
  * Projects using generators

If you're looking for something to work on, check the open issues or open an issue describing your idea before starting a larger change.

### Before Opening a PR

Please make sure that:

* the project still bootstraps successfully
* NightBuild can build itself
* relevant tests pass
* new behavior has regression coverage where appropriate
* the change does not introduce an unnecessary dependency
* the change does not add unnecessary architectural complexity

Please do not add third-party dependencies. Open an issue first if you believe one is necessary.

Please do not bloat the software or make large architecture changes. For architecture changes, open an issue before implementing them.

## Development

NightBuild is built with native C++23 and is intended to remain lightweight.

When working on the project, prefer:

* existing standard-library functionality over new dependencies
* small, focused changes
* straightforward implementations
* clear diagnostics over clever abstractions
* generation separate from execution
* explicit dependency graphs
* keeping `BUILD.nb` simple
* keeping `args.nb` focused on configuration
* keeping the generated command database concrete and deterministic

### Areas of the Codebase

The main areas of NightBuild are intentionally separated:

* **CLI** — command-line parsing, commands, help, and user-facing diagnostics
* **Manifest parser** — `BUILD.nb`, imports, expressions, targets, and `args.nb`
* **Build graph** — targets, dependencies, generators, and actions
* **Scheduler** — dependency-aware parallel execution
* **Toolchains** — compiler, linker, framework, and package configuration
* **Incremental state** — hashes, command state, and determining what needs rebuilding
* **Compile commands** — generation of `compile_commands.json`
* **TUI** — build progress and live status
* **Filesystem watching** — filesystem event handling
* **Tests** — regression coverage for the build system

A good contribution should generally fit cleanly into one of these areas rather than introducing a new abstraction across the entire project.

### Design Principles

NightBuild deliberately keeps generation and execution separate.

`nightbuild gen` should make the expensive build decisions up front.

`nightbuild args` should configure those decisions without turning `args.nb` into a second build language.

`nightbuild build` should execute the already-generated command graph efficiently.

When adding a feature, ask:

1. Does this solve a real build-system problem?
2. Does it make simple projects harder to understand?
3. Can it be implemented without adding significant machinery?
4. Does it belong in generation, configuration, or execution?
5. Can it be tested independently?

If a change requires significant architectural complexity, open an issue before implementing it.
