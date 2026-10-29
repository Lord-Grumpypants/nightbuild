#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REMOTE = ROOT / "third_party" / "remote-apis"
GOOGLE = ROOT / "third_party" / "googleapis"
GENERATED = ROOT / "src" / "remote" / "generated"
ENTRY = "build/bazel/remote/execution/v2/remote_execution.proto"
IMPORT_RE = re.compile(r'^\s*import\s+(?:public\s+|weak\s+)?"([^"]+)"\s*;', re.M)


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: generate_reapi.py <protoc> <grpc_cpp_plugin> <protobuf-include>")
    protoc, plugin, protobuf_include = sys.argv[1:]
    roots = [REMOTE, GOOGLE, Path(protobuf_include)]
    pending = [ENTRY]
    seen = set()
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        source = next((root / name for root in roots if (root / name).is_file()), None)
        if source is None:
            raise SystemExit("error: unresolved REAPI proto import: " + name)
        seen.add(name)
        pending.extend(IMPORT_RE.findall(source.read_text()))

    GENERATED.mkdir(parents=True, exist_ok=True)
    args = [protoc]
    for root in roots:
        args.extend(["--proto_path=" + str(root)])
    args.extend([
        "--cpp_out=" + str(GENERATED),
        "--grpc_out=" + str(GENERATED),
        "--plugin=protoc-gen-grpc=" + plugin,
    ])
    args.extend(sorted(seen))
    subprocess.run(args, cwd=ROOT, check=True)

    source_count = sum(
        1 for name in seen
        for suffix in (".pb.cc", ".grpc.pb.cc")
        if (GENERATED / (name[:-6] + suffix)).is_file()
    )
    print(
        f"REAPI · generated {len(seen)} official protobuf definitions "
        f"({source_count} translation units)",
        flush=True,
    )


if __name__ == "__main__":
    main()
