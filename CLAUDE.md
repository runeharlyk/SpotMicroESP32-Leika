# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

## Overview

Leika is the firmware, web app and simulation for SpotMicro-style quadrupeds on the ESP32, ESP32-S3 and ESP32-P4.
One firmware per board serves three robot variants (`SPOTMICRO_ESP32`, `SPOTMICRO_ESP32_MINI` "Spot Pico", `SPOTMICRO_YERTLE`), chosen at runtime as a robot setting.

- `esp32/` - the firmware: C++ on ESP-IDF, built with PlatformIO (`platformio.ini` at the root). The source of truth for motion.
- `app/` - the SvelteKit web controller: hosted on GitHub Pages and embedded in the firmware.
- `simulation/` - MuJoCo simulation, residual-gait RL and the Python `Robot` client, managed with `uv`.
- `platform_shared/` - the protobuf schema shared by all three, compiled by nanopb (firmware), ts-proto (app) and grpcio-tools (simulation). Generated code is gitignored.

The documentation in `docs/` is kept current; start from `docs/readme.md`.
`docs/software_description.md` describes the firmware's tasks, sensors and servo output, `docs/motion_system.md` the modes and gaits, `docs/websocket.md` and `docs/api.md` the protocol, and `docs/connectivity.md` the browser constraints that decide every transport (read it before proposing a connection flow).

## Commands

Firmware (from the root):

```sh
pio run -e esp32-s3-n8r2                 # build one env; see platformio.ini for the others
pio run -e esp32-s3-n8r2 -t upload       # build and flash
uv run --with pytest pytest esp32/test/host   # the firmware's host test programs (C++20 compiler as $CXX or g++)
```

Every firmware build compiles the protos and builds the embedded app, so it needs nanopb (`git submodule update --init --recursive`), `protoc` on `PATH` and Node with pnpm.
`esp32/include/secrets.h` (from `secrets.example.h`) is gitignored and needed to build.
The generated `sdkconfig.<env>` files keep explicit "is not set" lines that override a later `sdkconfig.defaults` entry; delete the generated file after changing the defaults.

App (from `app/`):

```sh
pnpm install && pnpm proto
pnpm dev                 # Vite on port 5173
pnpm check               # svelte-check
pnpm exec vitest run     # unit tests once (pnpm test:unit watches)
pnpm lint                # prettier --check and eslint
```

Simulation (from `simulation/`):

```sh
uv sync && uv run python gen_protos.py
uv run pytest -q
```

## Things that must stay in sync

- The firmware's gait and kinematics have two ports: TypeScript in `app/src/lib/simulation/firmware/` (the app's simulation) and NumPy in `simulation/src/robot/firmware_gait.py`.
  Both are pinned to traces the firmware itself produces (`esp32/test/host/export_firmware_traces.py`, run in `firmware-port-tests.yml`); change the motion code and regenerate the traces in the same change.
- Enums and message tags cross the wire by number, and the app on Pages may meet older firmware: never renumber or reuse a tag; retire it with `reserved` in `message.proto`.
- The variant dimensions exist in the firmware (`KIN_CONFIG_*` in `esp32/include/kinematics.h`) and the app (`app/src/lib/kinematics-variants.ts`); a unit test compares them.
- Hexapod (`../Hexapod`) is the sibling project on the same stack; a feature proven in one is ported to the other rather than redesigned.

## Firmware architecture in brief

- The control task (core 1, priority 5, 100 Hz) owns the motion state: it takes the newest input, mode and gait from `MotionInbox` once per tick, runs the motion state and the inverse kinematics, and writes all servos in one I2C burst.
- The sensor task (core 1, priority 4) reads the IMU and the slower sensors and runs bus work the socket queues.
- The service task brings up WiFi, the access point, mDNS and `esp_http_server`, and sends broadcasts and telemetry batches.
- Socket handlers never touch motion state directly; they post to the inbox. Telemetry leaves the control task through a lock-free ring.
- Settings are protobuf files in `/config` on LittleFS, each owned by a stateful service that validates writes.

## Conventions

- Commit messages are one line, a gitmoji and a present-tense verb: `✨ Adds ...`, `🐛 Fixes ...`.
- C++ formatting follows `esp32/.clang-format`; the app follows Prettier and ESLint.
- Tests run in CI per part: `firmware-port-tests.yml`, `frontend-tests.yml`, `simulation-tests.yml`, `proto-build.yml`, `embedded-build.yml`.
