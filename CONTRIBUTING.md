# Contributing

Read the [build guide](docs/BUILD_WINDOWS.md) and [testing guide](docs/TESTING.md)
before changing code. For a substantial feature or redesign, open an issue to
discuss the approach first.

## Reporting bugs

Include the Windows version, source revision, compiler, build configuration and
steps to reproduce the problem. Describe the expected result and attach relevant
log excerpts or a debugger stack when available. Do not upload game resource files.

## Submitting changes

Work on a topic branch from the default branch and keep each pull request focused
on one problem. Explain what changes for the player or developer and how you
verified it. Add regression coverage for correctness fixes and update affected
documentation.

Build both Debug and Release for changes to game or build code. Run relevant
automated tests and manual scenarios, resolve failing checks and review comments,
and preserve existing copyright notices. Avoid unrelated formatting or cleanup.

Maintainers handle merges, releases and repository access. Contributors can submit
pull requests from forks; write access is granted individually.

## Scope

Development focuses on a reliable Windows standalone game. Changes should preserve
existing gameplay and resource formats unless a behavior change is intentional
and documented. The DLL interface remains useful for Space Rangers integration.
See the [roadmap](docs/ROADMAP.md) for broader direction.

Game resources are supplied separately. Use synthetic data for automated tests
and keep local packages, configuration copies and generated files out of commits.
