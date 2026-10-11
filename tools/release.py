# MatrixGame - licensed under GPLv2 or any later version.
"""Validate a release tag and prepare an unpublished GitHub release from tested packages."""
import argparse
from datetime import date
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile


def command(*args, cwd=None, input=None):
    return subprocess.run(args, cwd=cwd, input=input, text=True, encoding="utf-8",
                          capture_output=True, check=True, timeout=300).stdout.strip()


def validate_tag(tag):
    if not re.fullmatch(r"[0-9]{4}\.[0-9]{2}\.[0-9]{2}(?:\.[1-9][0-9]*)?", tag):
        raise ValueError("Release tags must use YYYY.MM.DD or YYYY.MM.DD.N")
    date.fromisoformat(tag[:10].replace(".", "-"))


def verify_tag(root, tag):
    validate_tag(tag)
    # Fetch both refs again: a tag push need not refer to a commit on master.
    # A moved tag fails instead of silently replacing the checked-out version.
    command("git", "fetch", "origin", "+refs/heads/master:refs/remotes/origin/master",
            "refs/tags/" + tag + ":refs/tags/" + tag, cwd=root)
    revision = command("git", "rev-parse", "refs/tags/" + tag + "^{commit}", cwd=root)
    if command("git", "rev-parse", "HEAD", cwd=root) != revision:
        raise ValueError("Checked-out commit does not match the release tag")
    command("git", "merge-base", "--is-ancestor", revision, "refs/remotes/origin/master", cwd=root)
    return revision


def changelog_notes(text, tag):
    """Use only this tag's level-two section, never Unreleased or another release."""
    headings = list(re.finditer(r"^##[ \t]+(.+?)[ \t]*\r?$", text, re.MULTILINE))
    entries = []
    for index, heading in enumerate(headings):
        if re.fullmatch(r"(?:" + re.escape(tag) + r"|\[" + re.escape(tag) + r"\])", heading[1].strip()):
            end = headings[index + 1].start() if index + 1 < len(headings) else len(text)
            entry = text[heading.end():end].strip()
            if re.sub(r"<!--.*?-->", "", entry, flags=re.DOTALL).strip():
                entries.append(entry)
    if len(entries) > 1:
        raise ValueError("Duplicate changelog sections for " + tag)
    return entries[0] if entries else None


def package_assets(directory, revision):
    """Require checksummed source and x86; accept x64 only with matching PE metadata."""
    suffix = revision[:12]
    source = "MatrixGame-source-" + suffix + ".zip"
    binaries = {arch: "MatrixGame-windows-" + arch + "-" + suffix + ".zip" for arch in ("x86", "x64")}
    checksum = "MatrixGame-" + suffix + ".sha256"
    names = {path.name for path in directory.iterdir()}
    required = {source, binaries["x86"], checksum}
    if not required <= names or names - required - {binaries["x64"]}:
        raise ValueError("Expected only this revision's source, x86, optional x64 and checksum files")
    sums = {}
    for line in (directory / checksum).read_text(encoding="ascii").splitlines():
        match = re.fullmatch(r"([a-f0-9]{64})  ([A-Za-z0-9.-]+\.zip)", line)
        if not match or match[2] in sums:
            raise ValueError("Invalid or duplicate checksum entry")
        sums[match[2]] = match[1]
    if set(sums) != names - {checksum}:
        raise ValueError("Checksum list does not match the release archives")
    for name, expected in sums.items():
        if hashlib.sha256((directory / name).read_bytes()).hexdigest() != expected:
            raise ValueError("Checksum mismatch: " + name)
    architectures = []
    instructions = None
    for arch, name in binaries.items():
        if name not in names:
            continue
        with zipfile.ZipFile(directory / name) as archive:
            metadata = json.loads(archive.read("SOURCE.json"))
            if (metadata.get("revision") != revision or metadata.get("architecture") != arch
                    or metadata.get("configuration") != "Release" or metadata.get("cheats") is not False):
                raise ValueError("Package identity does not match the release: " + name)
            binary = archive.read("MatrixGame.exe")
            pe = struct.unpack_from("<I", binary, 60)[0]
            machine = struct.unpack_from("<H", binary, pe + 4)[0]
            magic = struct.unpack_from("<H", binary, pe + 24)[0]
            expected = (0x14c, 0x10b) if arch == "x86" else (0x8664, 0x20b)
            if binary[:2] != b"MZ" or binary[pe:pe + 4] != b"PE\0\0" or (machine, magic) != expected:
                raise ValueError("Executable architecture does not match " + arch)
            readme = archive.read("README.md").decode("utf-8").strip()
            if not readme:
                raise ValueError("Package installation instructions are empty")
            if instructions is None:
                instructions = readme
            elif instructions != readme:
                raise ValueError("Architecture packages must share installation instructions")
        architectures.append(arch)
    return [directory / name for name in sorted(names)], architectures, instructions


def api(repo, endpoint, *, method="GET", payload=None, paginate=False):
    args = ["gh", "api", "repos/" + repo + "/" + endpoint, "--method", method]
    if paginate:
        args += ["--paginate", "--slurp"]
    if payload is not None:
        args += ["--input", "-"]
    return json.loads(command(*args, input=json.dumps(payload) if payload is not None else None))


def assert_draft(release, tag, revision):
    if release.get("draft") is not True:
        raise ValueError("Refusing to modify a published release")
    if release.get("tag_name") != tag or release.get("target_commitish") != revision:
        raise ValueError("Existing draft does not identify this exact tag and commit")


def prepare_draft(root, tag, repo, directory):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
        raise ValueError("Expected an OWNER/REPOSITORY name")
    revision = verify_tag(root, tag)
    assets, architectures, instructions = package_assets(directory, revision)
    # Listing includes drafts; a failed API request must never mean 'not found'.
    matches = [release for page in api(repo, "releases?per_page=100", paginate=True)
               for release in page if release["tag_name"] == tag]
    if len(matches) > 1:
        raise ValueError("Multiple releases refer to this tag")
    existing = matches[0] if matches else None
    if existing:
        assert_draft(existing, tag, revision)
    changelog = root / "CHANGELOG.md"
    notes = changelog_notes(changelog.read_text(encoding="utf-8-sig"), tag) if changelog.exists() else None
    if notes is None:
        notes = api(repo, "releases/generate-notes", method="POST",
                    payload={"tag_name": tag, "target_commitish": revision})["body"].strip()
    if not notes:
        notes = "See the source revision below for the changes included in this release."
    downloads = "\n".join("- `" + path.name + "`" for path in assets)
    # Nest the packaged README under the release's installation section.
    instructions = re.sub(r"^(#{1,4}) ", r"##\1 ", instructions, flags=re.MULTILINE)
    body = (notes + "\n\n## Downloads\n\nWindows architectures: " + ", ".join(architectures)
            + ".\n\n" + downloads + "\n\nSource commit: `" + revision
            + "`. Verify downloaded archives against the SHA-256 file.\n\n"
            + "## Installation and requirements\n\n" + instructions + "\n")
    if existing:
        release = api(repo, "releases/" + str(existing["id"]))
        assert_draft(release, tag, revision)
        # Do not send draft=true on update: never turn a published release back into a draft.
        api(repo, "releases/" + str(release["id"]), method="PATCH", payload={"body": body})
    else:
        release = api(repo, "releases", method="POST", payload={"tag_name": tag,
                      "target_commitish": revision, "name": "MatrixGame " + tag,
                      "body": body, "draft": True})
    assert_draft(api(repo, "releases/" + str(release["id"])), tag, revision)
    command("gh", "release", "upload", tag, *[str(path) for path in assets], "--repo", repo, "--clobber")
    print("Draft prepared: " + release["html_url"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("verify", "draft"))
    parser.add_argument("--tag", required=True)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--repo")
    parser.add_argument("--packages", type=Path)
    args = parser.parse_args()
    if args.command == "verify":
        print("Verified release commit: " + verify_tag(args.root, args.tag))
    else:
        if not args.repo or not args.packages:
            parser.error("draft requires --repo and --packages")
        prepare_draft(args.root, args.tag, args.repo, args.packages.resolve())


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.stderr.strip() or str(error)) from error
    except (ValueError, OSError, KeyError, struct.error, zipfile.BadZipFile) as error:
        raise SystemExit(str(error)) from error
