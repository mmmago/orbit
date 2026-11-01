#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT
# Orbit file added: 2026-11-01 by Lucas Magotteaux.
"""Capture, verify and package source and legal material for macOS releases."""

import argparse
import concurrent.futures
import hashlib
import html
import io
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[2]
LEGAL = ROOT / "third-party/macos-arm64"
CELLAR = Path("/opt/homebrew/Cellar")
SOURCE_ASSET = "Orbit-sources-macos-arm64.tar.gz"


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def binaries(app):
    """Identify every third-party Mach-O, independent of its filename extension."""
    result = {}
    magics = {b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xce", b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf", b"\xbe\xba\xfe\xca", b"\xbf\xba\xfe\xca"}
    for path in sorted((app / "Contents").rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as stream:
            if stream.read(4) not in magics:
                continue
        relative = path.relative_to(app / "Contents")
        if relative.as_posix() in {"MacOS/Orbit", "MacOS/orbit-core", "MacOS/orbit-drag-helper"}:
            continue
        uuids = sorted(re.findall(r"UUID: ([A-Fa-f0-9-]+) \(([^)]+)\)", run("dwarfdump", "--uuid", str(path))))
        if not uuids:
            raise ValueError(f"No build UUID for {relative}")
        result[relative.as_posix()] = [list(pair) for pair in uuids]
    return result


def verify_app(app, manifest):
    expected = {file["path"]: file["uuids"] for component in manifest["components"] for file in component["files"]}
    actual = binaries(app)
    if not actual:
        raise ValueError("No third-party libraries found in staged app")
    if any(arch != manifest["architecture"] for pairs in actual.values() for _, arch in pairs):
        raise ValueError("Staged library architecture differs from source inventory")
    if actual != expected:
        added = sorted(actual.keys() - expected.keys())
        removed = sorted(expected.keys() - actual.keys())
        changed = sorted(k for k in actual.keys() & expected.keys() if actual[k] != expected[k])
        raise ValueError(f"Dependency inventory changed; recapture compliance material. Added: {added}; removed: {removed}; changed: {changed}")


def fetch(item, cache):
    target = cache / item["archive"]
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_file() and sha256(target) == item["sha256"]:
        return target
    partial = target.with_name(target.name + ".partial")
    urls = [item["url"], *item.get("mirrors", [])]
    for url in urls:
        if urlparse(url).scheme != "https":
            raise ValueError(f"Source URL must use HTTPS: {url}")
        result = subprocess.run(["curl", "--fail", "--location", "--silent", "--show-error", "--retry", "2", "--connect-timeout", "20", "--max-time", "600", "--output", str(partial), url])
        if result.returncode == 0 and sha256(partial) == item["sha256"]:
            partial.replace(target)
            print(f"Verified {item['archive']}", flush=True)
            return target
    partial.unlink(missing_ok=True)
    raise ValueError(f"Could not download a checksum-matching copy of {item['url']}")


def safe_member(name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"Unsafe source archive path: {name}")
    return path


def copy_notices(archive, destination):
    """Preserve upstream notice text without extracting/executing source files."""
    with tarfile.open(archive) as source:
        for entry in source:
            path = safe_member(entry.name)
            if not entry.isfile():
                continue
            if path.suffix in {".py", ".sh"} or path.name in {"update-copyright", "no-copyright"}:
                continue
            legal_name = re.search(r"(?i)(^|[._-])(licen[sc]e|copying|copyright|notice|authors)([._-]|$)", path.name)
            license_id_name = re.match(r"(?i)^(?:AGPL|GPL|LGPL|BSD|MIT|Apache|Unicode)[-_.].*\.(?:txt|md)$", path.name)
            if not (legal_name or license_id_name or "LICENSES" in path.parts or path.name.lower() in {"ftl.txt", "readme.ijg", "qt_attribution.json", "xxhash.h"}):
                continue
            if entry.size > 8 * 1024 * 1024:
                raise ValueError(f"Unexpectedly large legal file: {entry.name}")
            target = destination.joinpath(*path.parts)
            data = source.extractfile(entry).read()
            if path.name.lower() == "xxhash.h":
                # Preserve its copyright/license comment, rather than copying
                # executable hashing implementations into a notices directory.
                if not data.startswith(b"/*") or b"*/" not in data:
                    raise ValueError(f"Unrecognized license header in {entry.name}")
                data = data[:data.index(b"*/") + 2] + b"\n"
                target = target.with_name(target.name + ".LICENSE.txt")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)


def capture(args):
    metadata = json.loads(args.homebrew_metadata.read_text())
    components = {}
    for relative, uuids in binaries(args.app).items():
        path = PurePosixPath(relative)
        if path.parts[0] == "Frameworks":
            pattern = "*/*/lib/" + str(PurePosixPath(*path.parts[1:]))
        elif path.parts[0] == "PlugIns":
            pattern = "*/*/share/qt/plugins/" + str(PurePosixPath(*path.parts[1:]))
        else:
            raise ValueError(f"Unknown third-party code location: {relative}")
        origins = []
        for candidate in {p.resolve() for p in CELLAR.glob(pattern) if p.is_file()}:
            found = sorted(re.findall(r"UUID: ([A-Fa-f0-9-]+) \(([^)]+)\)", run("dwarfdump", "--uuid", str(candidate))))
            if [list(pair) for pair in found] == uuids:
                origins.append(candidate)
        if len(origins) != 1:
            raise ValueError(f"Cannot establish exact Homebrew provenance for {relative}: {origins}")
        origin = origins[0]
        formula, installed = origin.relative_to(CELLAR).parts[:2]
        component = components.setdefault((formula, installed), {"formula": formula, "installed_version": installed, "files": []})
        component["files"].append({"path": relative, "uuids": uuids, "cellar_relative_path": str(origin.relative_to(CELLAR))})

    LEGAL.mkdir(parents=True, exist_ok=True)
    all_downloads = []
    for (formula, installed), component in sorted(components.items()):
        keg = CELLAR / formula / installed
        recipe = (keg / ".brew" / (formula + ".rb")).read_text()
        sbom = json.loads((keg / "sbom.spdx.json").read_text())
        package = next(p for p in sbom["packages"] if p["SPDXID"].startswith("SPDXRef-Archive-") and p["name"] == formula)
        component.update({"version": package["versionInfo"], "package_license_expression": package["licenseConcluded"], "downloads": []})
        folder = LEGAL / formula
        folder.mkdir(exist_ok=True)
        (folder / (formula + ".rb")).write_text(recipe)
        (folder / "homebrew.spdx.json").write_text(json.dumps(sbom, indent=2) + "\n")

        def add_download(name, url, digest, kind, mirrors=None):
            entry = {"archive": f"{formula}/{name}", "url": url, "sha256": digest, "kind": kind}
            if mirrors:
                entry["mirrors"] = mirrors
            component["downloads"].append(entry)
            all_downloads.append(entry)

        digest = next(c["checksumValue"] for c in package["checksums"] if c["algorithm"] == "SHA256")
        url = package["downloadLocation"]
        mirrors = re.findall(r'^\s*mirror "(https://[^"\n]+)"', recipe, re.M)
        add_download(PurePosixPath(urlparse(url).path).name, url, digest, "source", mirrors)
        for patch in sbom["packages"]:
            if patch["SPDXID"].startswith("SPDXRef-Patch-"):
                digest = next(c["checksumValue"] for c in patch["checksums"] if c["algorithm"] == "SHA256")
                add_download(patch["SPDXID"].split("-")[-1] + ".patch", patch["downloadLocation"], digest, "patch")
        for path in re.findall(r'patch do\s+file "([^"]+)"', recipe):
            digest = metadata["formulae"][formula]["local_patch_checksums"][path]
            url = f"https://raw.githubusercontent.com/Homebrew/homebrew-core/{metadata['revision']}/{path}"
            add_download(path, url, digest, "patch")
        # Include formula resources too, including build-time code and test data.
        resource_pattern = r'resource "([^"]+)" do\s+url "([^"]+)"(?:(?!\n\s*end).)*?sha256 "([a-f0-9]{64})"'
        for name, url, digest in re.findall(resource_pattern, recipe, re.S):
            add_download("resources/" + PurePosixPath(urlparse(url).path).name, url, digest, "resource")
        component["recipe_sha256"] = sha256(folder / (formula + ".rb"))

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        errors = []
        for future in concurrent.futures.as_completed([pool.submit(fetch, item, args.cache) for item in all_downloads]):
            try:
                future.result()
            except (OSError, ValueError) as error:
                errors.append(str(error))
        if errors:
            raise ValueError("; ".join(errors))
    for component in components.values():
        folder = LEGAL / component["formula"] / "notices"
        if folder.exists():
            shutil.rmtree(folder)
        for item in component["downloads"]:
            if item["kind"] == "source":
                copy_notices(args.cache / item["archive"], folder)
        if not folder.exists() or not any(folder.rglob("*")):
            raise ValueError(f"No license text captured for {component['formula']}")

    # The recipes use Homebrew helper methods. Include their implementation too.
    revision = run("git", "-C", "/opt/homebrew", "rev-parse", "HEAD")
    helpers = args.cache / "Homebrew-build-scripts.tar.gz"
    subprocess.run(["git", "-C", "/opt/homebrew", "archive", "--format=tar.gz", "--prefix=Homebrew/", "-o", str(helpers.resolve()), revision], check=True)
    shutil.copyfile("/opt/homebrew/LICENSE.txt", LEGAL / "LICENSE-Homebrew.txt")
    patch_metadata = {"revision": metadata["revision"], "formulae": {formula: {"local_patch_checksums": metadata["formulae"][formula].get("local_patch_checksums", {})} for formula, _ in components}}
    write_json(LEGAL / "homebrew-patches.json", patch_metadata)
    manifest = {"schema": 1, "architecture": "arm64", "source_asset": SOURCE_ASSET, "homebrew_build_scripts": {"archive": helpers.name, "sha256": sha256(helpers), "revision": revision}, "components": list(sorted(components.values(), key=lambda c: c["formula"]))}
    manifest["legal_files"] = {str(p.relative_to(LEGAL)): sha256(p) for p in sorted(LEGAL.rglob("*")) if p.is_file() and p.name != "manifest.json"}
    write_json(LEGAL / "manifest.json", manifest)
    print(f"Captured {len(components)} components with complete source downloads, recipes and notices.")


def checked_manifest(cache=None):
    manifest = json.loads((LEGAL / "manifest.json").read_text())
    if manifest["schema"] != 1 or manifest["architecture"] != "arm64":
        raise ValueError("Unsupported dependency inventory format/architecture")
    for relative, digest in manifest["legal_files"].items():
        if sha256(LEGAL / relative) != digest:
            raise ValueError(f"Legal material changed: {relative}")
    if cache is not None:
        items = [item for component in manifest["components"] for item in component["downloads"]]
        items.append(manifest["homebrew_build_scripts"])
        for item in items:
            path = cache / item["archive"]
            if not path.is_file() or sha256(path) != item["sha256"]:
                raise ValueError(f"Missing or changed source archive: {path}; run capture first")
    return manifest


def bundle(args):
    manifest = checked_manifest(args.cache)
    verify_app(args.app, manifest)
    if run("git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=normal"):
        raise ValueError("Commit the release source before packaging; the source asset must match the build")
    revision = run("git", "-C", str(ROOT), "rev-parse", "HEAD")
    tag = run("git", "-C", str(ROOT), "describe", "--exact-match", "--match", "v*", "HEAD")
    release_url = f"https://github.com/mmmago/orbit/releases/tag/{tag}"
    source_url = f"https://github.com/mmmago/orbit/releases/download/{tag}/{SOURCE_ASSET}"
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / SOURCE_ASSET
    with tempfile.TemporaryDirectory(prefix="orbit-sources-") as temporary:
        orbit_source = Path(temporary) / "Orbit.tar.gz"
        subprocess.run(["git", "-C", str(ROOT), "archive", "--format=tar.gz", "--prefix=Orbit/", "-o", str(orbit_source), revision], check=True)
        def public_metadata(entry):
            entry.uid = entry.gid = 0
            entry.uname = entry.gname = "root"
            entry.pax_headers = {}
            return entry

        with tarfile.open(archive, "w:gz", compresslevel=1) as tar:
            prefix = "Orbit-sources-macos-arm64"
            tar.add(orbit_source, arcname=prefix + "/Orbit.tar.gz", filter=public_metadata)
            tar.add(LEGAL, arcname=prefix + "/third-party", filter=public_metadata)
            tar.add(ROOT / "docs/source-distribution.md", arcname=prefix + "/README.md", filter=public_metadata)
            tar.add(ROOT / "tools/compliance/macos.py", arcname=prefix + "/macos.py", filter=public_metadata)
            for component in manifest["components"]:
                for item in component["downloads"]:
                    tar.add(args.cache / item["archive"], arcname=prefix + "/downloads/" + item["archive"], filter=public_metadata)
            helpers = manifest["homebrew_build_scripts"]
            tar.add(args.cache / helpers["archive"], arcname=prefix + "/" + helpers["archive"], filter=public_metadata)
            data = (json.dumps({"tag": tag, "commit": revision, "source_url": source_url}, indent=2) + "\n").encode()
            info = tarfile.TarInfo(prefix + "/release.json")
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    legal = args.app / "Contents/Resources/Legal"
    shutil.copytree(LEGAL, legal / "third-party", dirs_exist_ok=True)
    shutil.copytree(ROOT / "LICENSES", legal / "LICENSES", dirs_exist_ok=True)
    shutil.copyfile(ROOT / "LICENSE", legal / "LICENSE")
    shutil.copyfile(ROOT / "NOTICE.md", legal / "NOTICE.md")
    shutil.copyfile(ROOT / "THIRD_PARTY_NOTICES.md", legal / "THIRD_PARTY_NOTICES.md")
    text = f"""# Orbit licenses and source code

Orbit {tag} — commit {revision}.

Orbit is free software, distributed under GNU GPL version 2 with the OpenSSL
exception, WITHOUT ANY WARRANTY. You may modify and redistribute it under those
terms. See LICENSE, LICENSES/ and NOTICE.md for the terms and original authors.
Third-party components retain their respective licenses; see THIRD_PARTY_NOTICES.md
and third-party/ for their notices, source identities and build recipes.

Release: {release_url}
Complete source download: {source_url}
Source archive SHA-256: {sha256(archive)}

The source archive includes Orbit's source and installation/build scripts, the
upstream source archives for every bundled third-party library, their patches,
Homebrew recipes/helper scripts and a checksummed inventory. Download it from the
same release page as the app. No signing private key is required to rebuild it.
"""
    (legal / "SOURCE_CODE.md").write_text(text)
    links = "".join(f'<li><a href="{html.escape(name)}">{html.escape(name)}</a></li>' for name in ["LICENSE", "NOTICE.md", "THIRD_PARTY_NOTICES.md", "SOURCE_CODE.md", "third-party/manifest.json"])
    (legal / "index.html").write_text(f'<!doctype html><html lang="en"><meta charset="utf-8"><title>Orbit — Licenses and Source Code</title><style>body{{font:17px system-ui;max-width:65em;margin:3em auto;padding:0 1em;line-height:1.5}}pre{{white-space:pre-wrap;overflow-wrap:anywhere}}</style><h1>Orbit — Licenses and Source Code</h1><p><a href="{html.escape(source_url)}">Download complete corresponding source</a> · <a href="{html.escape(release_url)}">Release page</a></p><ul>{links}</ul><pre>{html.escape(text)}</pre></html>\n')
    print(f"Verified all {len(binaries(args.app))} third-party binaries; included legal material and {archive}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prepare = commands.add_parser("capture", help="Capture legal material and checksum-verified upstream sources from the installed Homebrew packages")
    prepare.add_argument("--app", type=Path, required=True)
    prepare.add_argument("--cache", type=Path, required=True)
    prepare.add_argument("--homebrew-metadata", type=Path, default=LEGAL / "homebrew-patches.json", help="JSON with revision and formulae, including local_patch_checksums (defaults to the captured patch metadata)")
    prepare.set_defaults(action=capture)
    package = commands.add_parser("bundle", help="Fail closed on dependency changes; build source asset and install legal documents before signing")
    package.add_argument("--app", type=Path, required=True)
    package.add_argument("--cache", type=Path, required=True)
    package.add_argument("--output", type=Path, required=True)
    package.set_defaults(action=bundle)
    verify = commands.add_parser("verify", help="Check the staged library UUIDs and captured legal material without network access")
    verify.add_argument("--app", type=Path, required=True)
    verify.set_defaults(action=lambda args: verify_app(args.app, checked_manifest()))
    args = parser.parse_args()
    try:
        args.action(args)
    except (OSError, ValueError, subprocess.CalledProcessError, StopIteration) as error:
        parser.exit(1, f"Compliance packaging failed: {error or type(error).__name__}\n")


if __name__ == "__main__":
    main()
