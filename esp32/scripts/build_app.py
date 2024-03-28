#   ESP32 SvelteKit --
#
#   A simple, secure and extensible framework for IoT projects for ESP32 platforms
#   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
#   https://github.com/theelims/ESP32-sveltekit
#
#   Copyright (C) 2018 - 2023 rjwats
#   Copyright (C) 2023 theelims
#   Copyright (C) 2023 Maxtrium B.V. [ code available under dual license ]
#
#   All Rights Reserved. This software may be modified and distributed under
#   the terms of the LGPL v3 license. See the LICENSE file for details.

from pathlib import Path
from shutil import copyfileobj
import os
import gzip
import mimetypes
import glob
from datetime import datetime

Import("env")

output_file = env["PROJECT_DIR"] + "/lib/ESP32-sveltekit/WWWData.h"
source_www_dir = env["PROJECT_DIR"] + "/../app2/src"
build_www_dir = env["PROJECT_DIR"] + "/../app2/build"
www_dir = "www"

def find_latest_timestamp_for_app():
  list_of_files = glob.glob(source_www_dir+'/**/*', recursive=True) 
  latest_file = max(list_of_files, key=os.path.getmtime)
  return os.path.getmtime(latest_file)

def should_regenerate_output_file():
    if not flag_exists("EMBED_WWW") or not os.path.exists(output_file):
        return True
    last_source_change = find_latest_timestamp_for_app()
    last_build = os.path.getmtime(output_file)

    print(f'Newest interface file: {datetime.fromtimestamp(last_source_change):%Y-%m-%d %H:%M:%S}, WWW Outputfile: {datetime.fromtimestamp(last_build):%Y-%m-%d %H:%M:%S}')
    
    if (last_build < last_source_change):
        print("Svelte source files are updated. Need to regenerate.")
        return True
    print("Current outputfile is O.K. No need to regenerate.")
    return False


def gzip_file(file):
    with open(file, 'rb') as f_in:
        with gzip.open(file + '.gz', 'wb') as f_out:
            copyfileobj(f_in, f_out)
    os.remove(file)

def is_compressed_filetype(filetype):
    compressed_types = ['zip', 'gz', 'rar', '7z', 'jpg', 'jpeg', 'png', 'mp4', 'mp3']
    return filetype.lower() in compressed_types

def flag_exists(flag):
    buildFlags = env.ParseFlags(env["BUILD_FLAGS"])
    for define in buildFlags.get("CPPDEFINES"):
        if (define == flag or (isinstance(define, list) and define[0] == flag)):
            return True

def build_progmem():
    mimetypes.init()
    with open(output_file, "w") as progmem:
        progmem.write("#include <functional>\n")
        progmem.write("#include <Arduino.h>\n\n")

        assetMap = {}

        # Iterate over all files in the build directory
        for idx, path in enumerate(Path(www_dir).rglob("*.*")):
            asset_path = path.relative_to(www_dir).as_posix()
            print(f"Converting {asset_path}")

            asset_var = f'WEB_ASSET_DATA_{idx}'
            filetype = asset_path.split('.')[-1]
            asset_mime, _ = mimetypes.guess_type(asset_path)
            asset_mime = asset_mime if asset_mime else f'application/octet-stream'

            progmem.write('// ' + str(asset_path) + '\n')
            progmem.write('const uint8_t ' + asset_var + '[] = {\n  ')
            
            # Open path as binary file, compress and read into byte array
            size = 0
            with open(path, "rb") as f:
                file_data = f.read()
                if not is_compressed_filetype(filetype):
                    file_data = gzip.compress(file_data)
                for byte in file_data:
                    if not (size % 16):
                        progmem.write('\n\t')
                    
                    progmem.write(f"0x{byte:02X},")
                    size += 1

            progmem.write('\n};\n\n')
            assetMap[asset_path] = { "name": asset_var, "mime": asset_mime, "size": size }
        
        progmem.write('typedef std::function<void(const String& uri, const String& contentType, const uint8_t * content, size_t len)> RouteRegistrationHandler;\n\n')
        progmem.write('class WWWData {\n')
        progmem.write('\tpublic:\n')
        progmem.write('\t\tstatic void registerRoutes(RouteRegistrationHandler handler) {\n')

        for asset_path, asset in assetMap.items():
            progmem.write(f'\t\t\thandler("/{asset_path}", "{asset["mime"]}", {asset["name"]}, {asset["size"]});\n')

        progmem.write('\t\t}\n')
        progmem.write('};\n\n')

def build_webapp():
    os.chdir("../app2")
    print("Building app with pnpm")
    env.Execute("pnpm install")
    env.Execute("pnpm run build")
    os.chdir("../esp32")

def embed_webapp():
    if flag_exists("EMBED_WWW"):
        print("Converting interface to PROGMEM")
        build_progmem()
        return

print("running: build_interface.py")
if (should_regenerate_output_file()):
    build_webapp() 
embed_webapp()