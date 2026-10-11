# Preparing a release

Releases are Windows standalone packages built from tagged commits on `master`. Pushing a dated release tag starts the **Draft Windows release** workflow. It creates a draft for maintainer review; it never publishes the release automatically. Release promotion, tagging and publication follow [CONTRIBUTING.md](../CONTRIBUTING.md#releases).

## Maintainer procedure

1. Promote the reviewed and validated integration revision to `master`. The tagged commit must contain the release workflow and helpers. Wait for the required branch CI checks to pass.
2. If writing release notes, put them in the root `CHANGELOG.md` under `## YYYY.MM.DD` (or `## [YYYY.MM.DD]`). Use the exact intended tag; a second release on the same date uses `.1`, `.2`, and so on. Describe changes relevant to players, compatibility and known limitations. Commit the entry as part of the release promotion.
3. Fetch `master` and tag its intended commit. For example, for an authorized release named `2026.10.11`:

   ```powershell
   git fetch origin
   git tag -a 2026.10.11 origin/master -m "MatrixGame 2026.10.11"
   git push origin refs/tags/2026.10.11
   ```

4. Wait for **Draft Windows release** to finish. Open the draft in GitHub Releases and check its notes, downloads and installation requirements. Download the Windows archive, verify its checksum and perform the release smoke test with separately supplied resources.
5. Publish the draft only after maintainer approval. Editing a draft's title or notes for publication is supported.

Tags use `YYYY.MM.DD`, optionally followed by a positive `.N` without leading zeroes. Both annotated and lightweight tags work. The workflow fetches `master` and checks that it contains the tagged commit; a tag on an unpromoted development commit fails before building. Pushing a branch alone does not create a draft. Push a release tag after its commit reaches `master`; if promotion arrives later, rerun the failed workflow after promotion.

## Release contents and notes

The workflow runs the repository and Python tooling checks, builds the pinned MinGW x86 Release configuration without developer cheats, packages it, and runs the Release engine contracts. Only a successful build-and-test job can pass archives to the draft job. The build job has read-only repository permissions; only the draft job receives `contents: write`.

Downloads include the Windows ZIP, its matching source ZIP (including the required thread-runtime source), and a SHA-256 checksum file. Filenames contain the first twelve characters of the source commit. The Windows ZIP includes configuration, licenses and `README.md` with installation requirements. Game resource packages and private development files are excluded by the existing [packager](BUILD_WINDOWS.md).

The draft body uses the changelog section for the exact tag. `Unreleased`, entries for other tags, absent files, empty sections and comment-only sections do not supply release notes; in those cases the workflow calls GitHub's [release-notes generator](https://docs.github.com/en/rest/releases/releases#generate-release-notes-content-for-a-release). It then adds the download list, full source revision and installation instructions from the packaged README. The default requirements are Windows 10 or 11, the 32-bit June 2010 DirectX runtime component and a separately supplied `DATA/robots.pkg`; optional audio and configuration are described in that same README.

## Reruns and failures

Rerunning the tag workflow refreshes the notes and replaces matching assets of its existing draft. This allows recovery after a partial upload; complete the rerun before reviewing or editing the draft. A rerun overwrites manual draft-note edits. An existing published release is never intentionally modified, and an existing draft with a different source target is rejected. Do not edit or publish a draft while its workflow is running. Do not move a release tag to repair a published build; prepare a new tag instead.

No release is created if tag verification, build, tests, package checksums or note generation fail. An upload failure may leave an incomplete draft. Inspect the job log and rerun the failed jobs once the cause is resolved. A manually created draft must be removed or reconciled by its maintainer if it does not name the exact commit as its target. The workflow does not delete releases or unrelated assets.

## Architecture support

The current build and packager support x86 (32-bit), which also runs on 64-bit Windows. There is no x64 build yet, so the release contains no x64 archive. The draft helper already accepts an additional `MatrixGame-windows-x64-<commit>.zip` alongside x86, verifies its PE architecture and source metadata, and requires it in the checksum file. When x64 is implemented, extend the build job or add an architecture job, update the packager and shared installation instructions, and upload both verified packages in the `release-packages` artifact. Enable x64 delivery only after its builds and tests pass.

## Local validation

```powershell
python -B -m unittest discover -s tests -p 'test_release.py'
```

These tests use synthetic packages, temporary Git repositories and mocked GitHub requests. They check tag dates and ancestry, both tag types, changelog selection and fallback, archive identity and checksums, draft creation and reruns, and rejection of published releases. They create no GitHub releases. The actual tag-triggered workflow requires an authorized tag push after this workflow has reached `master`.
