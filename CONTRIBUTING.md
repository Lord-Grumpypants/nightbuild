## Contributing

We suggest you keep PRs **SMALL**, so review is easy.

We also suggest that you get familiar with the project, especially `BUILD.nb` and manifest semantics, before editing anything.

Here are some good first projects:

* Migrating tests from `nightbuild.toml` to `BUILD.nb`
* Improving documentation
* Making a script to generate PGO profiles

Please do not add third-party dependencies. Open an issue instead.

Please do not bloat the software or make architecture changes. For architecture changes, open an issue first.


## Development

NightBuild is built with native C++23 and is intended to remain lightweight.

When working on the project, prefer:

* existing standard-library functionality over new dependencies
* small, focused changes
* straightforward implementations
* keeping generation separate from execution
* preserving the simplicity of `BUILD.nb`, `args.nb`, and the generated command database

If a change requires significant architectural complexity, open an issue before implementing it.
