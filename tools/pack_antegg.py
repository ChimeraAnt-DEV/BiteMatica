#!/usr/bin/env python3
"""Assembles a `.AntEgg` package from a built mod tree.

`.AntEgg` is the GlowberryClient mod format: a ZIP archive with the `.antegg`
extension whose root holds `egg.json` plus the mod's files. This script lays out the
package exactly the way the loader's `AntEggPackage`/`AntEggLoader` expect, then writes
the archive with a fixed entry order and timestamps so two builds of the same inputs
produce byte-identical output.

Usage:
    python3 tools/pack_antegg.py \
        --manifest packaging/egg.json \
        --library build/android-arm64-v8a/out/arm64-v8a/libbitematica.so \
        --entry bin/libbitematica.so \
        --config packaging/config \
        --schematics assets/schematics \
        --out dist/BiteMatica-1.0.0.antegg
"""

import argparse
import json
import os
import stat
import sys
import zipfile


def fail(message):
    print("error: " + message, file=sys.stderr)
    sys.exit(1)


def collect_directory(root, prefix):
    """Yields (archive_path, source_path) for every file under root."""
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in sorted(filenames):
            source = os.path.join(dirpath, name)
            relative = os.path.relpath(source, root).replace(os.sep, "/")
            yield (prefix + "/" + relative if prefix else relative), source


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, help="path to egg.json")
    parser.add_argument("--library", required=True, help="path to the built .so")
    parser.add_argument("--entry", required=True, help="archive path of the .so")
    parser.add_argument("--config", help="directory packaged as config/")
    parser.add_argument("--schematics", help="directory packaged as schematics/")
    parser.add_argument("--extra", action="append", default=[],
                        help="extra 'archive/path=source' entries")
    parser.add_argument("--out", required=True, help="output .antegg path")
    args = parser.parse_args()

    if not os.path.isfile(args.manifest):
        fail("manifest not found: " + args.manifest)
    if not os.path.isfile(args.library):
        fail("library not found: " + args.library)

    with open(args.manifest, encoding="utf-8") as fp:
        manifest = json.load(fp)

    # The loader rejects a manifest whose entry_point is not inside the archive, so the
    # two must agree. Checking here turns a silent bad package into an obvious error.
    if manifest.get("entry_point") != args.entry:
        fail("manifest entry_point (%s) does not match --entry (%s)"
             % (manifest.get("entry_point"), args.entry))
    if not args.entry.lower().endswith(".so"):
        fail("a native entry_point must end with .so")

    entries = [("egg.json", args.manifest), (args.entry, args.library)]
    if args.config:
        if not os.path.isdir(args.config):
            fail("config directory not found: " + args.config)
        entries += list(collect_directory(args.config, "config"))
    if args.schematics:
        if not os.path.isdir(args.schematics):
            fail("schematics directory not found: " + args.schematics)
        entries += list(collect_directory(args.schematics, "schematics"))
    for extra in args.extra:
        if "=" not in extra:
            fail("--extra must be archive/path=source: " + extra)
        archive_path, source = extra.split("=", 1)
        if not os.path.isfile(source):
            fail("extra source not found: " + source)
        entries.append((archive_path, source))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)

    # Deterministic archive: sorted entries, fixed timestamp, fixed permissions.
    entries.sort(key=lambda item: item[0])
    fixed_date = (1980, 1, 1, 0, 0, 0)
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as archive:
        for archive_path, source in entries:
            info = zipfile.ZipInfo(archive_path, date_time=fixed_date)
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(source, "rb") as fp:
                archive.writestr(info, fp.read())

    size = os.path.getsize(args.out)
    print("wrote %s (%d bytes, %d entries)" % (args.out, size, len(entries)))
    for archive_path, _ in entries:
        print("  " + archive_path)


if __name__ == "__main__":
    main()
