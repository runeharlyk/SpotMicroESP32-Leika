# Software

The robot's firmware is built using PlatformIO with the ESP-IDF framework (`framework = espidf`).
The build resolves ESP-IDF 5.5.x (the component manager requires 5.0 or newer).

## Prerequisites

To prepare the frontend code for the ESP32, a specific build chain is required. Start by installing these essential tools:

### Required Software

Install the following software to ensure all functionality:

- [VSCode](https://code.visualstudio.com/) - Preferred IDE for development
- [PlatformIO](https://platformio.org/) Core 6.1.19 - CI pins this version, because Core 6.2 requires SCons 4.11, which the `esp32-p4` platform release cannot import
- [Node.js](https://nodejs.org) 20.19+ or 22.12+ - Needed for app building. `app/.npmrc` sets `engine-strict=true`, so an older Node fails the install
- [pnpm](https://pnpm.io) - The firmware build script uses it because the app ships a `pnpm-lock.yaml`. CI uses pnpm 9
- [protoc](https://github.com/protocolbuffers/protobuf/releases) on `PATH` - Generates the TypeScript protobuf bindings for the app. CI uses 27.x
- [Python 3.10 or higher](https://www.python.org/downloads/) - Used for firmware build scripts. The build installs `protobuf` and `grpcio-tools` into PlatformIO's Python if they are missing, and CI also installs `esp32/scripts/requirements.txt`
- [ClangFormat](https://releases.llvm.org/download.html) - Used for formatting (`esp32/.clang-format`)

### Project Structure

Understand the project organization through these key directories:

- [docs/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/docs)  - Documentation
- [app/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/app) - SvelteKit-based frontend
- [esp32](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/esp32) - Firmware for the robot
- [platform_shared/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/platform_shared) - Protobuf schemas shared by the firmware, the app and the simulation
- [simulation/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/simulation) - MuJoCo simulation and the Python robot client
- [flasher/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/flasher) - Web flasher page and manifest generator
- [boards/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/boards) - PlatformIO board definition for the ESP32-P4

### Git submodules

The firmware generates its protobuf code with [nanopb](https://github.com/nanopb/nanopb), which is included as a git submodule.
Clone with `git clone --recurse-submodules https://github.com/runeharlyk/SpotMicroESP32-Leika`.
If the repository is already cloned, run `git submodule update --init --recursive`.
Without it, the build fails because `submodules/nanopb` is empty.

## Setting up PlatformIO

### Configure Build Target

`platformio.ini` in the repository root defines one environment per supported board.
`default_envs` is `esp32-camera`; pick another with `-e <environment>` or in the PlatformIO tab.

| Environment          | Board                                                                      | Camera pinout   |
|----------------------|----------------------------------------------------------------------------|-----------------|
| `esp32-camera`       | ESP32-CAM (AI Thinker)                                                     | AI Thinker      |
| `esp32dev`           | ESP32 DevKit                                                               | no camera       |
| `esp32-wroom-camera` | ESP32-S3 DevKitC-1 with 8 MB flash                                         | ESP32-S3-EYE    |
| `seeed-xiao-esp32s3` | Seeed XIAO ESP32S3 Sense                                                   | XIAO ESP32S3    |
| `esp32-p4`           | ESP32-P4 dev board with ESP32-C6 co-processor (`boards/esp32p4_dev.json`)  | MIPI-CSI        |

The `esp32-p4` environment uses a pinned pioarduino platform release instead of `espressif32`.
CI builds all five environments.
For additional boards, add an environment based on the [official board list](https://docs.platformio.org/en/latest/boards/index.html#espressif-32).

One firmware per environment serves every robot variant and every combination of sensors.
The variant (`SPOTMICRO_ESP32`, `SPOTMICRO_ESP32_MINI` or `SPOTMICRO_YERTLE`) is chosen in the app after flashing, see [Choosing the variant](4_configuring.md#choosing-the-variant).
The I2C sensors are detected at boot, and each can be disabled on the Sensors page under Peripherals; the WS2812 strip is enabled and given its pin there too.
Only the camera is fixed by the environment, since its pins and driver come with the board.

### Factory settings

Update `esp32/factory_settings.ini` with the app name, hostname, access point settings and other device information.
The WiFi network and the passwords are not in this file; they come from `esp32/include/secrets.h`.

### WiFi and access point credentials

The first build copies `esp32/include/secrets.example.h` to `esp32/include/secrets.h` and prints a notice.
Git ignores `secrets.h`, so your credentials stay out of commits.
Put your network in `SECRET_WIFI_SSID` and `SECRET_WIFI_PASSWORD`, and build again to have the robot join it.
`SECRET_AP_PASSWORD` is the password of the robot's own access point, 8 to 64 characters.
It applies to a robot without stored settings, so after a fresh flash or a factory reset.
The build fails if `secrets.h` lacks a define that the example has, or if the access point password has the wrong length.

### Build & Upload Process

Build and upload from the repository root, or select the environment in the PlatformIO tab and click `Upload and Monitor`:

```sh
pio run -e esp32-camera -t upload
pio device monitor
```

A build does the following, in order:

1. Creates and checks `esp32/include/secrets.h`.
1. Compiles `platform_shared/*.proto` with nanopb into `esp32/src/platform_shared`.
1. Builds the app with `pnpm install` and `pnpm run build:embedded` in `app/`, gzips it and writes it to `esp32/include/WWWData.h`, which is compiled into the firmware. The app is rebuilt when `app/` or `platform_shared/` changed after `WWWData.h`.
1. Compiles the firmware. For every environment except `esp32-p4`, `esp32/scripts/merge_firmware.py` also writes the single flashable image `.pio/build/<environment>/firmware.factory.bin`.

The filesystem image does not need to be uploaded.
The app is part of the firmware, `esp32/data` is empty, and the robot writes its settings to the LittleFS partition at run time.
Uploading a filesystem image replaces those stored settings.

### Flash a prebuilt release

Every `v*` tag publishes a `<environment>.factory.bin` per environment on GitHub, and the [web flasher](https://runeharlyk.github.io/SpotMicroESP32-Leika/flash/) writes them from Chrome or Edge.
A prebuilt image is built from `secrets.example.h`: it joins no WiFi network, so connect to its access point as described in [Configuring](4_configuring.md).
