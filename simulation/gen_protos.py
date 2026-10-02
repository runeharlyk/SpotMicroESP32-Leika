"""Generates the Python classes for the robot's protocol: uv run python gen_protos.py

They land in src/proto/ (gitignored), from the same platform_shared/*.proto files the firmware and the app use.
"""
import os
import sys

from grpc_tools import protoc

HERE = os.path.dirname(os.path.abspath(__file__))
PROTOS = os.path.normpath(os.path.join(HERE, "..", "platform_shared"))
OUT = os.path.join(HERE, "src", "proto")


def main() -> int:
    files = [os.path.join(PROTOS, name) for name in ("filesystem.proto", "api.proto", "message.proto")]
    return protoc.main(["grpc_tools.protoc", f"-I{PROTOS}", f"--python_out={OUT}", *files])


if __name__ == "__main__":
    sys.exit(main())
