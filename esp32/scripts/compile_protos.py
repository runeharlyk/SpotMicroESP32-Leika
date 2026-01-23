#!/usr/bin/env python3
import subprocess
import os
import sys
from pathlib import Path

def ensure_protobuf_installed():
    """Ensure protobuf package is installed in the current Python environment."""
    try:
        import google.protobuf
        return True
    except ImportError:
        print("Installing required protobuf dependencies...")
        result = subprocess.run(
            [sys.executable, "-m", "pip", "install", "protobuf", "grpcio-tools"],
            capture_output=True,
            text=True
        )
        if result.returncode != 0:
            print(f"Failed to install protobuf: {result.stderr}")
            return False
        print("Protobuf dependencies installed successfully")
        return True

def get_project_root():
    script_dir = Path(__file__).parent.absolute()
    return script_dir.parent.parent

def compile_nanopb():
    project_root = get_project_root()
    proto_dir = project_root / "platform_shared"
    output_dir = project_root / "esp32" / "src" / "platform_shared"
    nanopb_gen = project_root / "submodules" / "nanopb" / "generator" / "nanopb_generator.py"
    
    output_dir.mkdir(parents=True, exist_ok=True)
    
    proto_files = [proto_dir / "filesystem.proto", proto_dir / "message.proto", proto_dir / "api.proto"]
    
    print(f"Compiling protobuf files with nanopb...")
    print(f"  Proto dir: {proto_dir}")
    print(f"  Output dir: {output_dir}")
    
    cmd = [
        sys.executable,
        str(nanopb_gen),
        "-I", str(proto_dir),
        "-D", str(output_dir),
    ] + [str(f) for f in proto_files]
    
    print(f"  Command: {' '.join(cmd)}")
    
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"Error compiling protos:")
        print(result.stderr)
        return False
    
    print(f"  Successfully compiled {len(proto_files)} proto files")
    return True

def main():
    if not ensure_protobuf_installed():
        print("Error: Failed to install protobuf dependencies")
        sys.exit(1)

    if not compile_nanopb():
        sys.exit(1)
    print("Proto compilation complete!")

if __name__ == "__main__":
    main()

