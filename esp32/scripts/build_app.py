from functools import lru_cache
from pathlib import Path
from os.path import exists, getmtime, splitext
import os
import gzip
import mimetypes
import glob
import zlib

Import("env")

project_dir = env["PROJECT_DIR"]
buildFlags = env.ParseFlags(env["BUILD_FLAGS"])

interface_dir = f"{project_dir}/app"
output_file = f"{project_dir}/esp32/include/WWWData.h"
source_www_dir = f"{interface_dir}/src"
build_dir = f"{interface_dir}/build"
filesystem_dir = f"{project_dir}/data"

Path(filesystem_dir).mkdir(exist_ok=True)
Path(output_file).parent.mkdir(parents=True, exist_ok=True)
mimetypes.init()

already_compressed_ext = {
    ".gz", ".br", ".png", ".jpg", ".jpeg", ".webp", ".gif", ".mp4", ".m4v", ".mov", ".avi", ".mkv", ".mp3", ".aac", ".ogg", ".wav",
    ".wasm", ".pdf", ".ico", ".woff", ".woff2", ".ttf", ".otf", ".7z", ".zip", ".rar", ".bz2", ".xz", ".lz", ".svgz"
}


@lru_cache(1)
def get_flag(flag, default=None):
    for d in buildFlags.get("CPPDEFINES", []):
        if d == flag:
            return True
        if isinstance(d, (list, tuple)) and d[0] == flag:
            return d[1] if len(d) > 1 else True
    return default


def get_files_to_exclude():
    files_to_exclude = []
    if (get_flag("SPOTMICRO_ESP32") or get_flag("SPOTMICRO_ESP32_MINI")) and not get_flag("SPOTMICRO_YERTLE"):
        print("Excluding Yertle files for SPOTMICRO_ESP32 build")
        files_to_exclude.extend(["yertle.URDF", "URDF.zip", "URDF/"])
    elif get_flag("SPOTMICRO_YERTLE") and not get_flag("SPOTMICRO_ESP32") and not get_flag("SPOTMICRO_ESP32_MINI"):
        print("Excluding Spot Micro files for SPOTMICRO_YERTLE build")
        files_to_exclude.extend(["spot_micro.urdf.xacro", "stl.zip", "stl/"])
    else:
        print("No specific variant flag set, including all files")
    return files_to_exclude


def latest_ts():
    files = [p for p in glob.glob(
        f"{source_www_dir}/**/*", recursive=True) if os.path.isfile(p)]
    return max(getmtime(p) for p in files) if files else 0


def needs_rebuild():
    if not exists(output_file):
        return True
    return getmtime(output_file) < latest_ts()


def pkg_mgr():
    if exists(os.path.join(interface_dir, "pnpm-lock.yaml")):
        return "pnpm"
    if exists(os.path.join(interface_dir, "yarn.lock")):
        return "yarn"
    if exists(os.path.join(interface_dir, "package-lock.json")):
        return "npm"


def build_web():
    m = pkg_mgr()
    if not m:
        raise Exception(
            "No lock-file found. Please install dependencies for interface")
    cwd = os.getcwd()
    try:
        os.chdir(interface_dir)
        env.Execute(f"{m} install")
        env.Execute(f"{m} run build:embedded")
    finally:
        os.chdir(cwd)


def encode_asset_data(path):
    ext = splitext(path.name)[1].lower()
    raw = path.read_bytes()
    if ext in already_compressed_ext:
        return raw, 0, zlib.crc32(raw) & 0xFFFFFFFF
    gz = gzip.compress(raw, mtime=0)
    return gz, 1, zlib.crc32(gz) & 0xFFFFFFFF


def write_header():
    exclude = get_files_to_exclude()
    assets = []
    for p in sorted(Path(build_dir).rglob("*.*"), key=lambda x: x.relative_to(build_dir).as_posix()):
        rel_path = p.relative_to(build_dir).as_posix()
        if any(rel_path == ex or rel_path.startswith(ex.rstrip("/")) for ex in exclude):
            continue
        uri = "/" + rel_path
        mime = mimetypes.guess_type(uri)[0] or "application/octet-stream"
        data, gz_flag, etag = encode_asset_data(p)
        assets.append((uri, mime, data, gz_flag, etag))

    offsets, cursor = [], 0
    for _, _, data, _, _ in assets:
        offsets.append(cursor)
        cursor += len(data)

    with open(output_file, "w", newline="\n") as f:
        f.write("#pragma once\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(
            "struct WebAsset { const char* uri; const char* mime; const uint8_t* data; uint32_t len; uint32_t etag; uint8_t gz; };\n")
        f.write(
            "struct WebOptions { const char* default_uri; uint32_t max_age; uint8_t add_vary; };\n\n")

        f.write("static const uint8_t WWW_BLOB[] PROGMEM = {\n")
        col = 0
        for _, _, data, _, _ in assets:
            for b in data:
                if col == 0:
                    f.write("\t")
                f.write(f"0x{b:02X},")
                col = (col + 1) % 16
                if col == 0:
                    f.write("\n")
        if col != 0:
            f.write("\n")
        f.write("};\n\n")

        for i, (uri, _, _, _, _) in enumerate(assets):
            f.write(f'static const char WWW_URI_{i}[] PROGMEM = "{uri}";\n')
        for i, (_, mime, _, _, _) in enumerate(assets):
            f.write(f'static const char WWW_MIME_{i}[] PROGMEM = "{mime}";\n')
        f.write("\n")

        f.write("static const WebAsset WWW_ASSETS[] PROGMEM = {\n")
        for i, (_, _, data, gz_flag, etag) in enumerate(assets):
            f.write(
                f"\t{{WWW_URI_{i}, WWW_MIME_{i}, WWW_BLOB+{offsets[i]}, {len(data)}, 0x{etag:08X}, {gz_flag}}},\n")
        f.write("};\n\n")

        f.write(f"static const size_t WWW_ASSETS_COUNT = {len(assets)};\n")
        default_uri = "/index.html" if any(u == "/index.html" for u,
                                           _, _, _, _ in assets) else (assets[0][0] if assets else "/")
        f.write(
            f'static const WebOptions WWW_OPT = {{ "{default_uri}", 31536000u, 1 }};\n')


if get_flag("EMBED_WEBAPP") == "1" and needs_rebuild():
    print("Building web app")
    build_web()
    write_header()
