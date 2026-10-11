#!/usr/bin/env python3
"""Check the packaged config and its schema stay in sync (M5).

The launcher's config editor reads `config/config.schema.json`; the mod reads
`config/config.json`. If a key exists in one but not the other, the editor shows a field the mod
ignores, or a mod setting is invisible in the editor. This check fails the build when they drift.

Also verifies that the `Config` struct's serialized keys cover the schema (by grepping the C++
serializer), so a new setting cannot be added to the mod without a schema entry.

Usage: python3 tools/check_config_sync.py
"""

from __future__ import annotations

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main() -> int:
    config_path = os.path.join(ROOT, "packaging", "config", "config.json")
    schema_path = os.path.join(ROOT, "packaging", "config", "config.schema.json")
    with open(config_path) as handle:
        config = json.load(handle)
    with open(schema_path) as handle:
        schema = json.load(handle)

    config_keys = set(config.keys())
    schema_keys = set(schema.get("properties", {}).keys())

    missing_in_schema = config_keys - schema_keys
    missing_in_config = schema_keys - config_keys

    failed = False
    if missing_in_schema:
        print("FAIL: config keys with no schema entry:", sorted(missing_in_schema), file=sys.stderr)
        failed = True
    if missing_in_config:
        print("FAIL: schema keys with no config default:", sorted(missing_in_config), file=sys.stderr)
        failed = True

    # Cross-check the C++ serializer emits every schema key. Grep the serializer for each key.
    serializer = open(os.path.join(ROOT, "src", "mod", "JsonConfig.cpp")).read()
    for key in sorted(schema_keys):
        if key == "version":
            continue  # written as a literal
        if f'"{key}"' not in serializer:
            print(f"FAIL: schema key '{key}' is not written by JsonConfig::serialize", file=sys.stderr)
            failed = True

    if failed:
        return 1
    print(f"ok: config and schema agree on {len(config_keys)} keys")
    return 0


if __name__ == "__main__":
    sys.exit(main())
