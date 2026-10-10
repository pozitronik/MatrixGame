# Standalone packaging

The supported package is a minimal Windows x86 MinGW Release ZIP with cheats disabled. It contains the executable, committed text configuration, player README, notices, license texts and `SOURCE.json`. The source ZIP contains committed project source and pinned MCF Gthread source. DirectX and game packages are supplied separately.

## Prepare archives

Use the pinned tools and Python 3.9 or newer, from a clean committed checkout:

```powershell
.\tools\setup-toolchain.ps1
.\tools\package.ps1
```

`-Python <path>` selects Python; `-Jobs <number>` controls compilation; `-OutputDirectory <path>` selects another archive directory. The default is `build/packages`. The helper always rebuilds Release with resource staging disabled, records the source/binary/cache hashes, verifies the x86 GUI executable and rejects unexpected non-system imports. There is no unchecked reuse option. The DirectX prerequisite follows [Microsoft's deployment guidance](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/directx-setup-for-game-developers).

Only `MatrixGame.exe` and explicitly selected source/configuration/notice files enter the binary ZIP. Staged archives, private mappings, logs, diagnostic executables and stray DLLs are never enumerated into it. Configuration comes from the committed tree, rather than a possibly contaminated build directory. Tracked changes or untracked production source prevent packaging. Existing archives with different bytes are preserved and cause an error; select an empty output directory to package a changed executable at the same revision.

The helper emits binary/source ZIPs and a `.sha256` file. Sorted names, fixed ZIP timestamps, fixed permissions and stored entries make identical inputs produce identical archives across locations. MinGW maps compile-time checkout paths to repository-relative source names; Release links with a zero PE timestamp. The packager rejects an executable retaining its source/build directory in UTF-8 or UTF-16LE, with either Windows separator spelling, and rejects a nonzero COFF timestamp. These remove known checkout-location and relink-time differences; they do not promise bit-identical recompilation across compilers, machines or changed dependencies. Source metadata identifies the revision actually built, including a CI merge revision when applicable, and the player's audio-guide link targets that revision. The package carries no matching contributor checkout paths or session data.

MCF Gthread source is fetched by immutable commit and verified by SHA256, cached under `.tools/downloads`. A matching cache supports offline packaging. Its GPL/LGPL text is preserved verbatim. The source ZIP contains `vendor/mcfgthread-source.tar.gz` and its source/hash metadata. To relink with a modified runtime, build that source for i686 using the pinned compiler and its upstream build instructions, replace the toolchain's `i686-w64-mingw32/lib/libmcfgthread.a`, and rebuild the application with `tools/build.ps1`; keep the runtime ABI and distribute the modified source with its notices. The source archive includes the application code and build scripts needed to rebuild the executable. Do not replace installed shared prerequisites during an ordinary validation run.

## Verify a candidate

Run `python -B -m unittest discover -s tests -p 'test_package.py'`. Synthetic fixtures check file selection, privacy, deterministic archives, source pairing, build/import rejection and stale receipts. They neither use game assets nor run an executable.

Extract the binary ZIP into an otherwise empty writable directory. Use an installed or separately prepared x86 DirectX runtime. First launch without game packages: confirm the placement dialog and exit 1. Then privately stage `DATA/robots.pkg`, use the distributed text configuration, check startup and window-close exit 0. Repeat with optional audio resources when relevant and record any missing checks. Keep supplied packages out of the ZIP and reports. Archive creation alone does not establish playable battle behavior.

## Release checklist

- Confirm independent approval and required CI for the promoted revision.
- Complete the standalone milestone's required gameplay, lifecycle, input, audio and device checks, with limitations recorded.
- Prepare the package from committed source with the supported helper; verify checksums and inspect binary/source contents and notices.
- Validate the extracted package, resource-placement behavior and supplied-resource launch on the supported Windows setup.
- Publish the matching source archive and checksum file with the binary archive so recipients can inspect and rebuild it.
- Obtain maintainer authorization for release promotion, a dated tag and publication, following `CONTRIBUTING.md`.

The helper creates local files only. Packaging preparation and CI artifacts do not publish a release, create a tag or establish milestone completion. MSVC and Debug builds remain supported development configurations; they are outside this initial distribution format.
