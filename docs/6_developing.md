# Developing

> _Prerequisites_: You have successfully built, flashed, and configured your robot, and installed the tools in [Software](3_software.md).

## Setting up SvelteKit

### Proxy Configuration for Development

The app opens its WebSocket at `ws://<address>/api/ws`.
With an address set on the start page or on `/connection`, the browser talks to the robot directly.
With no address, the app uses the host that serves it, so in development the Vite proxy in `app/vite.config.ts` forwards `/api` to the robot:

```ts
server: {
    proxy: {
        '/api': {
            target: 'http://spot-micro.local/', // Here
            changeOrigin: true,
            ws: true
        }
    }
},
```

The factory hostname is `spot-micro-<id>.local`, so change the target to your robot's hostname or IP, or set the address in the app instead.

> Changes require a restart of the development server.

### Development server

```sh
cd app
pnpm install
pnpm proto
pnpm dev
```

`pnpm proto` writes the generated protobuf bindings to `app/src/lib/platform_shared`, which git ignores, so run it again after a change in `platform_shared/*.proto`.
`pnpm dev` runs `pnpm model` first, which copies the simulation models from `simulation/src/resources` into `app/static`.

## Before a pull request

CI runs the checks below for the paths they cover, on every push and pull request to `master`.
Run the ones for the code you changed.

### Web app (`app/`)

```sh
cd app
pnpm install
pnpm proto
pnpm lint        # prettier --check . && eslint .
pnpm check       # svelte-kit sync && svelte-check
pnpm exec playwright install chromium
pnpm test        # Playwright integration tests, then Vitest unit tests
```

- `pnpm format` rewrites the files with Prettier (`app/.prettierrc`).
- `pnpm test:integration` builds the app and serves it with `vite preview` on port 4173 (`app/playwright.config.ts`), so it needs `protoc`.
- `pnpm test` ends in `vitest`, which stays in watch mode in a terminal. Use `pnpm exec vitest run` for a single run, or `pnpm exec vitest run tests/unit/<file>.spec.ts` for one file.
- Unit tests are in `app/tests/unit`, integration tests in `app/tests/integration`.

This is the `Frontend Tests` workflow, triggered by changes in `app/**` and `simulation/src/resources/**`.

### Firmware (`esp32/`, `platform_shared/`, `boards/`, `platformio.ini`)

The `PlatformIO CI` workflow builds all six environments with PlatformIO Core 6.1.19:

```sh
pio run -e esp32-camera
pio run -e esp32dev
pio run -e esp32-wroom-camera
pio run -e esp32-s3-n8r2
pio run -e seeed-xiao-esp32s3
pio run -e esp32-p4
```

Build at least the environment you changed, and the others when you touched shared code.
Format C++ with ClangFormat (`esp32/.clang-format`); no workflow checks it.

### Firmware host tests (`esp32/`, `platform_shared/`, `app/src/lib/simulation/firmware/`)

The `Firmware Host Tests` workflow compiles the firmware headers with a C++20 host compiler (`$CXX`, or `g++`) and runs them.
It needs the nanopb submodule.
From the repository root:

```sh
uv run --with protobuf --with grpcio-tools python esp32/scripts/compile_protos.py
uv run --with pytest pytest -q esp32/test/scripts esp32/test/host
uv run python esp32/test/host/export_firmware_traces.py
cd app
pnpm proto
pnpm exec vitest run tests/unit/firmware-motion.spec.ts
```

- `esp32/test/scripts` tests the secrets check, and `esp32/test/host` builds and runs each C++ test program.
- `export_firmware_traces.py` regenerates `app/tests/fixtures/firmware-trace-*.json` from the firmware code. The web app's TypeScript port of the motion code is tested against them, so commit the regenerated files when the motion code changes.

### Simulation (`simulation/`, `platform_shared/`)

The `Simulation Tests` workflow needs Python 3.13 or newer and uv:

```sh
cd simulation
uv sync --locked
uv run python gen_protos.py
uv run pytest -q
```

It also runs when `app/static/spot_micro.urdf.xacro`, `app/static/yertle.URDF` or `app/static/URDF/**` change.
`uv run python export_gait_trace.py` regenerates `app/tests/fixtures/pico-gait-trace.json`, which the app's `pico-gait.spec.ts` checks.

### Protobuf schemas (`platform_shared/`)

The `Proto Build` workflow compiles the schemas for the firmware (`python esp32/scripts/compile_protos.py`) and for the app (`pnpm proto`).
A schema change must build for both, and for the simulation (`gen_protos.py`).

## Releases

Pushing a tag `v*` runs the `Release` workflow: it builds all environments and publishes each `firmware.factory.bin` as `<environment>.factory.bin` on a GitHub release, then redeploys the web flasher.
The `Deploy GitHub Pages` workflow publishes the hosted app and the flasher on every push to `master`.
