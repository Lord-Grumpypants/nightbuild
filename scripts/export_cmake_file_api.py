#!/usr/bin/env python3
"""Export a CMake File API codemodel into a stable JSON inventory.

The export preserves CMake's target metadata instead of trying to translate it
prematurely into NightBuild syntax. In particular, target dependencies and
link command fragments are recorded separately: CMake's codemodel dependency
edges describe build dependencies and are not, by themselves, a complete
link-interface representation.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise RuntimeError(f"cannot read {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"invalid JSON in {path}: {exc}") from exc

    if not isinstance(value, dict):
        raise RuntimeError(f"expected a JSON object in {path}")
    return value


def latest_index(reply_dir: Path) -> Path:
    indexes = sorted(reply_dir.glob("index-*.json"))
    if not indexes:
        raise RuntimeError(
            f"no CMake File API index found in {reply_dir}; "
            "configure CMake after creating the codemodel-v2 query"
        )
    return indexes[-1]


def target_record(
    target: dict[str, Any],
    target_json_path: str,
) -> dict[str, Any]:
    """Keep the fields needed for source, compile, dependency and link analysis."""
    fields = (
        "id",
        "name",
        "type",
        "nameOnDisk",
        "artifacts",
        "paths",
        "sources",
        "sourceGroups",
        "compileGroups",
        "dependencies",
        "link",
        "archive",
        "backtrace",
        "backtraceGraph",
    )

    record = {key: target[key] for key in fields if key in target}
    record["targetJson"] = target_json_path
    return record


def export_configuration(
    reply_dir: Path,
    configuration: dict[str, Any],
    roots: list[str],
) -> dict[str, Any]:
    entries = configuration.get("targets", [])
    targets_by_id: dict[str, dict[str, Any]] = {}

    for entry in entries:
        json_file = entry.get("jsonFile")
        if not isinstance(json_file, str):
            raise RuntimeError(
                f"target entry in configuration "
                f"{configuration.get('name', '<unnamed>')} has no jsonFile"
            )

        target_path = reply_dir / json_file
        target = read_json(target_path)
        target_id = target.get("id")
        if not isinstance(target_id, str):
            raise RuntimeError(f"target response {target_path} has no id")

        targets_by_id[target_id] = target_record(target, json_file)

    ids_by_name: dict[str, str] = {}
    for target_id, target in targets_by_id.items():
        name = target.get("name")
        if isinstance(name, str):
            if name in ids_by_name:
                raise RuntimeError(
                    f"duplicate target name {name!r} in configuration "
                    f"{configuration.get('name', '<unnamed>')}"
                )
            ids_by_name[name] = target_id

    selected_ids: set[str] | None = None
    if roots:
        missing = [name for name in roots if name not in ids_by_name]
        if missing:
            available = sorted(ids_by_name)
            preview = ", ".join(available[:40])
            suffix = " ..." if len(available) > 40 else ""
            raise RuntimeError(
                "unknown root target(s): "
                + ", ".join(missing)
                + f"\nAvailable targets: {preview}{suffix}"
            )

        selected_ids = set()
        pending = [ids_by_name[name] for name in roots]
        while pending:
            target_id = pending.pop()
            if target_id in selected_ids:
                continue
            selected_ids.add(target_id)

            target = targets_by_id[target_id]
            for dependency in target.get("dependencies", []):
                dependency_id = dependency.get("id")
                if dependency_id in targets_by_id:
                    pending.append(dependency_id)

    exported_targets: list[dict[str, Any]] = []
    for target_id, target in sorted(
        targets_by_id.items(),
        key=lambda item: (str(item[1].get("name", "")), item[0]),
    ):
        if selected_ids is not None and target_id not in selected_ids:
            continue

        dependencies = []
        for dependency in target.get("dependencies", []):
            dependency_id = dependency.get("id")
            child = targets_by_id.get(dependency_id)
            dependencies.append(
                {
                    "id": dependency_id,
                    "name": child.get("name") if child else None,
                    "backtrace": dependency.get("backtrace"),
                }
            )

        target["dependencies"] = dependencies
        exported_targets.append(target)

    type_counts = Counter(
        target.get("type", "UNKNOWN") for target in exported_targets
    )

    return {
        "name": configuration.get("name", ""),
        "targetCount": len(exported_targets),
        "targetTypeCounts": dict(sorted(type_counts.items())),
        "roots": roots,
        "targets": exported_targets,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Export CMake File API codemodel target metadata to JSON."
        )
    )
    parser.add_argument(
        "--reply-dir",
        type=Path,
        default=Path("build/.cmake/api/v1/reply"),
        help=(
            "CMake File API reply directory "
            "(default: build/.cmake/api/v1/reply)"
        ),
    )
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        default=Path("cmake-file-api.json"),
        help="output JSON path; use '-' for stdout",
    )
    parser.add_argument(
        "--configuration",
        help="export only this CMake configuration (for example Release)",
    )
    parser.add_argument(
        "--root",
        action="append",
        default=[],
        help=(
            "target name whose dependency closure should be exported; "
            "may be repeated"
        ),
    )
    args = parser.parse_args()

    try:
        reply_dir = args.reply_dir.expanduser().resolve()
        index_path = latest_index(reply_dir)
        index = read_json(index_path)

        codemodel_ref = index.get("reply", {}).get("codemodel-v2")
        if not isinstance(codemodel_ref, dict):
            raise RuntimeError(
                f"{index_path} has no codemodel-v2 response; "
                "create .cmake/api/v1/query/codemodel-v2 and reconfigure"
            )

        codemodel_file = codemodel_ref.get("jsonFile")
        if not isinstance(codemodel_file, str):
            raise RuntimeError("codemodel-v2 response has no jsonFile")

        codemodel = read_json(reply_dir / codemodel_file)
        configurations = codemodel.get("configurations", [])
        if not configurations:
            raise RuntimeError("CMake codemodel contains no configurations")

        if args.configuration:
            configurations = [
                config
                for config in configurations
                if config.get("name") == args.configuration
            ]
            if not configurations:
                available = [
                    config.get("name", "")
                    for config in codemodel.get("configurations", [])
                ]
                raise RuntimeError(
                    f"configuration {args.configuration!r} not found; "
                    f"available: {', '.join(available)}"
                )

        result = {
            "schemaVersion": 1,
            "generator": codemodel.get("generator", {}),
            "paths": codemodel.get("paths", {}),
            "cmake": {
                "indexFile": index_path.name,
                "codemodelFile": codemodel_file,
            },
            "semantics": {
                "dependencies": (
                    "CMake codemodel target dependency edges. These are "
                    "build dependencies and may not include every link-only "
                    "interface requirement."
                ),
                "link": (
                    "Preserved verbatim from each target response. Inspect "
                    "link.commandFragments when deriving final link inputs."
                ),
                "compileGroups": (
                    "Preserved verbatim, including language standards, "
                    "definitions, include paths, and compile fragments."
                ),
            },
            "configurations": [
                export_configuration(reply_dir, config, args.root)
                for config in configurations
            ],
        }

        rendered = json.dumps(
            result,
            indent=2,
            sort_keys=True,
            ensure_ascii=False,
        ) + "\n"

        if str(args.output) == "-":
            sys.stdout.write(rendered)
        else:
            output = args.output.expanduser()
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(rendered, encoding="utf-8")
            print(f"Wrote {output}", file=sys.stderr)

        for config in result["configurations"]:
            counts = ", ".join(
                f"{kind}={count}"
                for kind, count in config["targetTypeCounts"].items()
            )
            print(
                f"{config['name'] or '<default>'}: "
                f"{config['targetCount']} targets"
                + (f" ({counts})" if counts else ""),
                file=sys.stderr,
            )

    except RuntimeError as exc:
        print(f"cmake-file-api-export: error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
