"""Guards the wire numbers of platform_shared/*.proto against renumbering and removal.

wire_golden.json lists the number of every enum value and every message field, oneof members included.
A peer built from older schemas decodes by these numbers, so changing or dropping one breaks it silently.

    uv run --with protobuf --with grpcio-tools python platform_shared/wire_golden.py           # update
    uv run --with protobuf --with grpcio-tools python platform_shared/wire_golden.py --check   # CI

--check fails when a listed number changed or disappeared, and when the schemas hold numbers the golden
lacks (run the update and commit wire_golden.json with the schema change).
A field or enum value may go once its number is reserved; the update then drops it from the golden.
The update refuses any other renumbering or removal unless --allow-breaking says it is deliberate.
"""
import argparse
import json
import sys
import tempfile
from pathlib import Path

from google.protobuf import descriptor_pb2
from grpc_tools import protoc

HERE = Path(__file__).resolve().parent
GOLDEN = HERE / "wire_golden.json"
UPDATE_COMMAND = "uv run --with protobuf --with grpcio-tools python platform_shared/wire_golden.py"


def _descriptor_set(proto_dir: Path) -> descriptor_pb2.FileDescriptorSet:
    protos = sorted(path.name for path in proto_dir.glob("*.proto"))
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "set.pb"
        status = protoc.main(["grpc_tools.protoc", f"-I{proto_dir}", f"--descriptor_set_out={out}", *protos])
        if status != 0:
            raise RuntimeError(f"protoc failed on {proto_dir} with status {status}")
        return descriptor_pb2.FileDescriptorSet.FromString(out.read_bytes())


def _reserved(ranges, end_inclusive: bool) -> set[int]:
    return {n for r in ranges for n in range(r.start, r.end + 1 if end_inclusive else r.end)}


class Schema:
    """Wire numbers by qualified type name, plus the numbers each type reserves."""

    def __init__(self) -> None:
        self.enums: dict[str, dict[str, int]] = {}
        self.messages: dict[str, dict[str, int]] = {}
        self.reserved: dict[str, set[int]] = {}

    def _add_enum(self, name: str, enum: descriptor_pb2.EnumDescriptorProto) -> None:
        self.enums[name] = {value.name: value.number for value in enum.value}
        self.reserved[name] = _reserved(enum.reserved_range, end_inclusive=True)

    def _add_message(self, name: str, message: descriptor_pb2.DescriptorProto) -> None:
        self.messages[name] = {field.name: field.number for field in sorted(message.field, key=lambda f: f.number)}
        self.reserved[name] = _reserved(message.reserved_range, end_inclusive=False)
        for nested in message.nested_type:
            self._add_message(f"{name}.{nested.name}", nested)
        for enum in message.enum_type:
            self._add_enum(f"{name}.{enum.name}", enum)

    @classmethod
    def from_protos(cls, proto_dir: Path) -> "Schema":
        schema = cls()
        for file in _descriptor_set(proto_dir).file:
            prefix = f"{file.package}." if file.package else ""
            for message in file.message_type:
                schema._add_message(prefix + message.name, message)
            for enum in file.enum_type:
                schema._add_enum(prefix + enum.name, enum)
        return schema

    def golden(self) -> dict:
        return {"enums": dict(sorted(self.enums.items())), "messages": dict(sorted(self.messages.items()))}


def compare(golden: dict, schema: Schema) -> tuple[list[str], list[str], list[str]]:
    """Returns (breaking, retired, added): changes that break older peers, removals behind a reserved
    number, and numbers the golden does not list yet."""
    breaking, retired, added = [], [], []
    for kind, current in (("enums", schema.enums), ("messages", schema.messages)):
        listed = golden.get(kind, {})
        for type_name, members in listed.items():
            if type_name not in current:
                breaking.append(f"{type_name} was removed")
                continue
            for member, number in members.items():
                now = current[type_name].get(member)
                if now == number:
                    continue
                if now is not None:
                    breaking.append(f"{type_name}.{member} was renumbered from {number} to {now}")
                elif number in schema.reserved[type_name]:
                    retired.append(f"{type_name}.{member} = {number} was removed and its number reserved")
                else:
                    breaking.append(f"{type_name}.{member} = {number} was removed without reserving its number")
        for type_name, members in current.items():
            for member, number in members.items():
                if member not in listed.get(type_name, {}):
                    added.append(f"{type_name}.{member} = {number}")
    return breaking, retired, added


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fail unless the golden matches the schemas")
    parser.add_argument("--allow-breaking", action="store_true", help="update the golden despite breaking changes")
    parser.add_argument("--proto-dir", type=Path, default=HERE)
    parser.add_argument("--golden", type=Path, default=GOLDEN)
    args = parser.parse_args(argv)

    schema = Schema.from_protos(args.proto_dir)
    golden = json.loads(args.golden.read_text()) if args.golden.exists() else {}
    breaking, retired, added = compare(golden, schema)

    for line in breaking:
        print(f"BREAKING: {line}", file=sys.stderr)
    if args.check:
        stale = retired + added
        for line in stale:
            print(f"not in {args.golden.name}: {line}", file=sys.stderr)
        if stale:
            print(f"Update the golden: {UPDATE_COMMAND}", file=sys.stderr)
        return 1 if breaking or stale else 0

    if breaking and not args.allow_breaking:
        print("Refusing to update the golden; reserve removed numbers, or pass --allow-breaking.", file=sys.stderr)
        return 1
    args.golden.write_text(json.dumps(schema.golden(), indent=2) + "\n", newline="\n")
    print(f"Wrote {args.golden} ({len(added)} added, {len(retired)} retired, {len(breaking)} breaking)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
