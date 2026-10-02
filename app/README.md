# Spot Micro controller

The SvelteKit web app for the robot.
It is built three ways:

- `pnpm dev` serves it with Vite for development.
- `pnpm build` produces the static site that GitHub Pages hosts (`BASE_PATH` sets the sub-path).
- `pnpm build:embedded` produces the build that the firmware embeds in its flash, without the 3D view and the simulation. The firmware build runs it, see [Software](../docs/3_software.md).

## Developing

Requirements: Node.js 20.19+ or 22.12+ (`.npmrc` sets `engine-strict`), pnpm, and `protoc` on `PATH`.

```sh
pnpm install
pnpm proto        # generates src/lib/platform_shared from ../platform_shared (git ignores it)
pnpm dev          # runs `pnpm model` first, then Vite on port 5173
```

`pnpm model` copies the simulation models from `../simulation/src/resources` into `static/`.
`pnpm build` runs `proto` and `model` itself, and `pnpm preview` serves the result on port 4173.

## Checks

```sh
pnpm lint         # prettier --check . && eslint .
pnpm check        # svelte-kit sync && svelte-check
pnpm test         # Playwright integration tests (tests/integration), then Vitest (tests/unit)
pnpm format       # prettier --write .
```

The first Playwright run needs `pnpm exec playwright install chromium`.
`pnpm test:unit` stays in watch mode in a terminal; use `pnpm exec vitest run` for one run.
More in [Developing](../docs/6_developing.md).
