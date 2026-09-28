# Software

The robot's firmware is built using PlatformIO with the Arduino framework over ESP-IDF.

## Prerequisites

To prepare the frontend code for the ESP32, a specific build chain is required. Start by installing these essential tools:

### Required Software

Install the following software to ensure all functionality:

- [VSCode](https://code.visualstudio.com/) - Preferred IDE for development
- [Node.js](https://nodejs.org) 20.19+ or 22.12+ - Needed for app building
- [pnpm](https://pnpm.io) - The firmware build script uses it because the app ships a `pnpm-lock.yaml`
- [protoc](https://github.com/protocolbuffers/protobuf/releases) on `PATH` - Generates the TypeScript protobuf bindings for the app
- [Python 3.8 or higher](https://www.python.org/downloads/) - Used for firmware build scripts
- [ClangFormat](https://releases.llvm.org/download.html) - Used for formatting

### Project Structure

Understand the project organization through these key directories:

- [docs/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/docs)  - Documentation
- [app/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/app) - SvelteKit-based frontend
- [esp32](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/esp32) - Firmware for the robot

### Git submodules

The firmware generates its protobuf code with [nanopb](https://github.com/nanopb/nanopb), which is included as a git submodule.
Clone with `git clone --recurse-submodules https://github.com/runeharlyk/SpotMicroESP32-Leika`.
If the repository is already cloned, run `git submodule update --init --recursive`.
Without it, the build fails because `submodules/nanopb` is empty.

## Setting up PlatformIO

### Configure Build Target

Modify the `platformio.ini` file at [platformio.ini](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/esp32/platformio.ini) to match your board specifications. Adapt or remove environment settings as necessary based on your board.

```ini
[platformio]
...
default_envs = esp32dev
...

[env:esp32cam]
board = esp32cam
board_build.mcu = esp32c3
```

For additional boards, refer to the [official board list](https://docs.platformio.org/en/latest/boards/index.html#espressif-32).

### Factory settings

Update the `esp32/factory_setting.ini` with new Wi-Fi settings, app name and other device information.

### Build & Upload Process

Update the `platformio.ini` file for your board, then navigate to the PlatformIO tab, select your environment, click `Upload Filesystem Image` and after uploading finishes, click `Upload and Monitor`. The filesystem image only needs to be uploaded the first time. It will override config files on the microcontroller.
When uploading new firmware, the app is evaluated, and if necessary, will be rebuilt.
