#!/usr/bin/env python3
"""Verifies a built `.AntEgg` is loadable on the launcher it targets.

Two things have to hold for a native mod to load:

1. Its `.so` must export `PLGetModRegistration` (the preloader's lifecycle entry point).
2. Every undefined symbol that the running preloader does *not* export must be **weak**, or
   `dlopen` fails and the launcher reports "Built for a different game build (missing symbol)".

The second rule is the one that bites: the mod's world-space renderer uses `pl::render::*`, which
only newer preloader builds export. Those symbols are declared weak so the mod still loads on an
older preloader (and falls back to its software overlay), and this script asserts that.

Usage:
    python3 tools/verify_antegg.py dist/BiteMatica-1.1.3.antegg
    python3 tools/verify_antegg.py dist/*.antegg --require-weak pl::render::
"""

import argparse
import re
import subprocess
import sys
import zipfile


def read_symbols(so_path):
    """Returns (defined_globals, weak_undefined, strong_undefined) from readelf."""
    try:
        output = subprocess.check_output(["readelf", "-sW", so_path], text=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        sys.exit("could not read symbols from %s: %s" % (so_path, exc))

    defined = set()
    weak_undef = set()
    strong_undef = set()
    for line in output.splitlines():
        # Columns: Num: Value Size Type Bind Vis Ndx Name
        parts = line.split()
        if len(parts) < 8:
            continue
        try:
            ndx = parts[6]
        except IndexError:
            continue
        bind = parts[4]
        name = parts[7]
        if ndx == "UND":
            if bind == "WEAK":
                weak_undef.add(name)
            else:
                strong_undef.add(name)
        elif bind in ("GLOBAL", "WEAK"):
            defined.add(name)
    return defined, weak_undef, strong_undef


def demangle(names):
    if not names:
        return {}
    try:
        result = subprocess.run(["c++filt"], input="\n".join(names), text=True,
                                capture_output=True, check=True)
        return dict(zip(names, result.stdout.splitlines()))
    except (OSError, subprocess.CalledProcessError):
        return {name: name for name in names}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("package", help="the .antegg to check")
    parser.add_argument("--require-weak", action="append", default=[],
                        help="a symbol substring that MUST be weak-undefined (repeatable)")
    parser.add_argument("--entry", default="bin/libbitematica.so")
    args = parser.parse_args()

    failures = []

    with zipfile.ZipFile(args.package) as archive:
        names = archive.namelist()
        if args.entry not in names:
            sys.exit("package has no %s" % args.entry)
        so_bytes = archive.read(args.entry)

    import os
    import tempfile
    with tempfile.NamedTemporaryFile(suffix=".so", delete=False) as handle:
        handle.write(so_bytes)
        so_path = handle.name

    try:
        defined, weak_undef, strong_undef = read_symbols(so_path)
        print("defined symbols: %d, weak-undefined: %d, strong-undefined: %d"
              % (len(defined), len(weak_undef), len(strong_undef)))

        if not any("PLGetModRegistration" in name for name in defined):
            failures.append("the .so does not export PLGetModRegistration")

        demangled_weak = demangle(sorted(weak_undef))
        demangled_strong = demangle(sorted(strong_undef))

        for required in args.require_weak:
            matching = [n for n in weak_undef if required in n] + \
                       [n for n, d in demangled_weak.items() if required in d]
            if not matching:
                # It may be defined by a newer preloader; only a problem if it is strong-undefined.
                strong_matching = [n for n in strong_undef if required in n] + \
                                  [n for n, d in demangled_strong.items() if required in d]
                if strong_matching:
                    failures.append("symbol %s is strongly undefined (would fail dlopen on an "
                                    "older preloader): %s"
                                    % (required, ", ".join(sorted(set(strong_matching))[:3])))
                else:
                    print("note: %s not undefined at all (fine)" % required)
            else:
                print("ok: %s is weak-undefined (%d symbol(s))" % (required, len(matching)))

        # Core preloader symbols (pl::mod/modmenu/input/memory) are legitimately strong-undefined:
        # every preloader build exports them, so dlopen resolves them. Only the optional APIs named
        # by --require-weak have to be weak. This lists the strong-undefined preloader symbols for
        # visibility, but only fails on ones that are not known-core.
        core_prefixes = ("pl::mod::", "pl::modmenu::", "pl::input::", "pl::memory::",
                         "pl::cosmetics::Mat4::", "pl::cosmetics::Vec3::",
                         "pl::cosmetics::composeCosmeticMatrix")
        unexpected = []
        for name in sorted(strong_undef):
            if not (name.startswith("_ZN2pl") or name.startswith("_ZNK2pl")):
                continue
            rendered = demangled_strong.get(name, name)
            if not rendered.startswith(core_prefixes):
                unexpected.append(rendered)
        if unexpected:
            failures.append("strongly undefined non-core preloader symbols: "
                            + ", ".join(unexpected[:5]))
    finally:
        os.unlink(so_path)

    if failures:
        print("\nFAILED:")
        for failure in failures:
            print("  - " + failure)
        return 1

    print("\nOK: the package will load on a preloader that lacks the render API")
    return 0


if __name__ == "__main__":
    sys.exit(main())
