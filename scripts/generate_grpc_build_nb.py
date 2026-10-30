#!/usr/bin/env python3
"""Generate NightBuild's vendored gRPC target graph from a CMake File API export.

The inventory must contain the complete dependency closure rooted at grpc++.
Static-library targets remain separate archives. OBJECT_LIBRARY sources are
folded into their consuming static archive (BoringSSL fipsmodule -> crypto).
"""

from __future__ import annotations

import argparse
import json
import shlex
import shutil
import sys
from pathlib import Path, PurePosixPath
from typing import Any

REPO_GRPC_ROOT = PurePosixPath("build/reapi/grpc")
COMPILE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}


def fail(message: str) -> None:
    raise RuntimeError(message)


def quote(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def read_inventory(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read inventory {path}: {exc}")
    if data.get("schemaVersion") != 1:
        fail("expected CMake File API inventory schemaVersion 1")
    configs = data.get("configurations")
    if not isinstance(configs, list) or len(configs) != 1:
        fail("inventory must contain exactly one configuration")
    if not isinstance(configs[0].get("targets"), list):
        fail("inventory configuration has no targets list")
    return data


def normalize_path(
    raw: str,
    source_root: Path,
    build_root: Path,
    generated: bool = False,
) -> str:
    value = Path(raw)
    if not value.is_absolute():
        value = (build_root if generated else source_root) / value
    value = value.resolve(strict=False)

    for root in (source_root, build_root):
        try:
            relative = value.relative_to(root)
            return (REPO_GRPC_ROOT / PurePosixPath(relative.as_posix())).as_posix()
        except ValueError:
            continue
    return value.as_posix()


def target_sources(
    target: dict[str, Any],
    source_root: Path,
    build_root: Path,
) -> tuple[list[str], set[str]]:
    sources: list[str] = []
    languages: set[str] = set()
    groups = target.get("compileGroups", [])

    for source in target.get("sources", []):
        raw = source.get("path")
        if not isinstance(raw, str) or Path(raw).suffix.lower() not in COMPILE_SUFFIXES:
            continue
        index = source.get("compileGroupIndex")
        if not isinstance(index, int) or not 0 <= index < len(groups):
            fail(f"{target.get('name')}: no compile group for source {raw}")
        language = groups[index].get("language")
        if language not in {"C", "CXX"}:
            fail(f"{target.get('name')}: unsupported language {language!r} for {raw}")
        languages.add(language)
        path = normalize_path(
            raw, source_root, build_root, bool(source.get("isGenerated"))
        )
        if path not in sources:
            sources.append(path)
    return sources, languages


def compile_settings(
    target: dict[str, Any],
    source_root: Path,
    build_root: Path,
) -> tuple[list[str], list[str], list[str]]:
    flags: dict[str, list[str]] = {"C": [], "CXX": []}
    include_dirs: list[str] = []

    for group in target.get("compileGroups", []):
        language = group.get("language")
        if language not in flags:
            continue
        for fragment in group.get("compileCommandFragments", []):
            raw = fragment.get("fragment", "")
            try:
                tokens = shlex.split(raw)
            except ValueError as exc:
                fail(f"{target.get('name')}: invalid compile flags {raw!r}: {exc}")
            for token in tokens:
                if token not in flags[language]:
                    flags[language].append(token)
        for define in group.get("defines", []):
            value = define.get("define")
            if isinstance(value, str) and "-D" + value not in flags[language]:
                flags[language].append("-D" + value)
        for include in group.get("includes", []):
            raw = include.get("path")
            if isinstance(raw, str):
                value = normalize_path(
                    raw, source_root, build_root, bool(include.get("isGenerated"))
                )
                if value not in include_dirs:
                    include_dirs.append(value)
    return flags["C"], flags["CXX"], include_dirs


def render_list(name: str, values: list[str]) -> list[str]:
    if not values:
        return []
    return [f"    {name} = [", *[
        f"        {quote(value)}," for value in values
    ], "    ]"]


def generate(data: dict[str, Any]) -> str:
    config = data["configurations"][0]
    targets = config["targets"]
    by_name = {target.get("name"): target for target in targets}
    if len(by_name) != len(targets) or None in by_name:
        fail("target inventory contains missing or duplicate names")
    if "grpc++" not in by_name:
        fail("inventory must be exported with --root grpc++")

    paths = data.get("paths", {})
    source_value, build_value = paths.get("source"), paths.get("build")
    if not isinstance(source_value, str) or not isinstance(build_value, str):
        fail("inventory is missing CMake source/build roots")
    source_root, build_root = Path(source_value).resolve(), Path(build_value).resolve()

    static_targets = {
        name: target for name, target in by_name.items()
        if target.get("type") == "STATIC_LIBRARY"
    }
    object_targets = {
        name: target for name, target in by_name.items()
        if target.get("type") == "OBJECT_LIBRARY"
    }

    object_sources: dict[str, list[str]] = {}
    for name, target in object_targets.items():
        sources, _ = target_sources(target, source_root, build_root)
        consumers = [
            parent_name for parent_name, parent in static_targets.items()
            if any(dep.get("name") == name for dep in parent.get("dependencies", []))
        ]
        if not consumers:
            fail(f"object library {name} has no static-library consumer")
        for consumer in consumers:
            object_sources.setdefault(consumer, []).extend(sources)

    output = [
        "# Generated by scripts/generate_grpc_build_nb.py.",
        "# Source of truth: the configured vendored gRPC CMake File API inventory.",
        "# Regenerate after changing the pinned dependency graph.",
        "",
    ]

    for name in sorted(static_targets):
        target = static_targets[name]
        sources, languages = target_sources(target, source_root, build_root)
        sources.extend(path for path in object_sources.get(name, []) if path not in sources)
        if not sources:
            fail(f"static library {name} has no compilable sources")

        cflags, cxxflags, includes = compile_settings(target, source_root, build_root)
        if name == "grpc":
            # c-ares ares.h includes ares_build.h from its source-root directory.
            cares_root = (REPO_GRPC_ROOT / "third_party/cares").as_posix()
            if cares_root not in includes:
                includes.append(cares_root)
        deps: list[str] = []
        for dependency in target.get("dependencies", []):
            dep_name = dependency.get("name")
            if dep_name in static_targets and dep_name not in deps:
                deps.append(dep_name)

        output.append(f"static_library({quote(name)}):")
        output.append("    toolchain_include = [")
        if "CXX" in languages:
            output.append('        "toolchains/CXX.json",')
        if "C" in languages:
            output.append('        "toolchains/C.json",')
        output.append("    ]")
        output.extend(render_list("sources", sources))
        output.extend(render_list("include_dirs", includes))
        output.extend(render_list("cflags", cflags))
        output.extend(render_list("cxxflags", cxxflags))
        output.extend(render_list("deps", deps))
        output.append("")

    return "\n".join(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inventory", type=Path, help="exported CMake inventory JSON")
    parser.add_argument(
        "--output", type=Path, default=Path("third_party/grpc.BUILD.nb"),
        help="output NightBuild file (default: third_party/grpc.BUILD.nb)",
    )
    parser.add_argument(
        "--no-backup", action="store_true",
        help="do not preserve an existing output as OUTPUT.bak",
    )
    args = parser.parse_args()

    try:
        data = read_inventory(args.inventory.expanduser())
        rendered = generate(data)
        output = args.output.expanduser()
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists() and not args.no_backup:
            backup = output.with_name(output.name + ".bak")
            if backup.exists():
                fail(f"refusing to overwrite existing backup {backup}")
            shutil.copy2(output, backup)
            print(f"Backup: {backup}", file=sys.stderr)
        output.write_text(rendered, encoding="utf-8")
        count = sum(
            target.get("type") == "STATIC_LIBRARY"
            for target in data["configurations"][0]["targets"]
        )
        print(f"Wrote {output}: {count} static-library targets", file=sys.stderr)
    except (RuntimeError, OSError) as exc:
        print(f"grpc-build-generator: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
