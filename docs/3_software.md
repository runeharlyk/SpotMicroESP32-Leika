# Software

The robots firmware is built using platform io using the arduino framework over ESP-IDF.

## Prerequisites

To prepare the frontend code for the ESP32, a specific build chain is required. Start by installing these essential tools:

### Required Software

Install the following software to ensure all functionalities:

- [VSCode](https://code.visualstudio.com/) - Preferred IDE for development
- [Node.js](https://nodejs.org) - Needed for app building
- A package manager of your choice (npm, pnpm, yarn)
- [Python 3.8 or higher](https://www.python.org/downloads/) - Used for firmware build scripts
- [ClangFormat](https://releases.llvm.org/download.html) - Used for formatting

### Project Structure

Understand the project organization through these key directories:

- [docs/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/docs)  - Documentation
- [app/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/app) - SvelteKit-based frontend
- [esp32](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/esp32) - Firmware for the robot

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

Update the `esp32/factory_setting.ini` with new wifi settings, app name and other device information.

### Build & Upload Process

Update the `platformio.ini` file for your board, then navigate to the PlatformIO tab, select your environment, click `Upload Filesystem Image` and after uploading finish, click `Upload and Monitor`. The filesystem image only has to be uploaded the first time and will override config files on the microcontroller.
When uploading new firmware the app is evaluated and if necessary will be rebuild.
