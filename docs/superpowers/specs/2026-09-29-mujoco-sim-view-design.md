# MuJoCo simulation view - design

Date: 2026-09-29.
Status: draft for review.

## Goal

The hosted web app gets a "Simulation" view in which a physically simulated Pico walks under the same joystick, mode and speed controls that drive the real robot.
It works with no robot connected, so the controller can be tried, and gaits compared, from any Chromium or Firefox browser.
Success: in the view, Stand holds the Pico upright, Walk with the left stick pushed forward moves it forward, and the right stick turns it, with the same gait the Python simulation (`simulation/`) produces for the same command.

Out of scope for this spec: running the trained residual policy, simulating Spot Micro or Yertle (they have no MJCF), driving the real robot from the simulation, and settling whether the Pico URDF or the firmware MINI geometry is correct.

## Findings that shape the design

- `simulation/src/resources/spot_pico/scene.xml` is a complete MJCF: free-floating base, 12 hinge joints, 12 position actuators (kp 4.0), foot geoms and sites, IMU sensors, timestep 0.002 s, meshes from `meshes/`.
- The Python controller is `simulation/src/robot/firmware_gait.py`: a port of the firmware's `walk_state.h` gait engine with inverse kinematics derived from the MJCF, not from the firmware's constants.
  Its IK matches MuJoCo forward kinematics to 0.0 mm, and its output is actuator targets in radians in MJCF order `[fr, fl, rr, rl] x [hip, femur, tibia]`.
- `sim/mj_runtime.py` steps physics 5 times per 10 ms control tick (100 Hz, the firmware rate).
- MuJoCo publishes official browser bindings, `@mujoco/mujoco` 3.14: a 293 KB loader and a 10.3 MB single-threaded WASM module, plus a multi-threaded build.
  The multi-threaded build needs `SharedArrayBuffer`, which needs COOP/COEP headers that GitHub Pages cannot set, so only the single-threaded build is usable.
- The app already renders the Pico from the same URDF and mesh zip (`pnpm model`), with joint names identical to the MJCF's.

## Decisions

1. Engine: `@mujoco/mujoco`, single-threaded build, loaded with a dynamic `import()` only when the Simulation view opens; the WASM is a separate asset fetched then, so the rest of the app does not grow by 10 MB.
2. Hosted app only: the view is behind `HAS_3D_VIEW`, so the firmware's built-in app never contains it.
3. Model: the simulation's own `scene.xml` and meshes, delivered by the existing `pnpm model` step (one copy of the robot, shared by Python simulation, 3D view and browser simulation).
4. Controller: a TypeScript port of `firmware_gait.py` (gait engine, MJCF IK, command mapping, `gait_coef.json`), because it is the controller the Python simulation and the training use; the app's existing `gait.ts` and `kinematic.ts` use the firmware's Spot geometry and joint conventions, which do not map onto the MJCF.
5. Drift guard: a Python script exports a golden trace from `firmware_gait.py` (fixed command sequence, per-tick feet and joint targets) and a Vitest test requires the TypeScript port to reproduce it to 1e-9.
6. Always the Pico: the simulation runs the Pico whatever robot is connected, since it is the only robot with an MJCF, and the view says so.
7. Digital twin, not a replacement: the controls keep going to a connected robot as today; the simulation reads the same `input` and `mode` stores and never writes to the socket.

## Units

- `app/src/lib/simulation/pico-gait.ts`: port of `GaitController`, `Kinematics`, `leg_ik`, `analytic_gait_action`, `GaitState`, `BodyState` and the geometry table.
  Pure TypeScript, no MuJoCo, no DOM.
- `app/src/lib/simulation/pico-sim.ts`: owns the MuJoCo model and data; `reset()`, `step(dtSeconds)` which runs whole 10 ms control ticks (5 physics steps each) and keeps the remainder, `setCommand(mode, vx, vy, yaw)`, and read-outs `basePose()` and `jointAngles()`.
  Loads `scene.xml` and the meshes into MuJoCo's in-memory file system.
- `app/src/lib/simulation/controls.ts`: maps the `input` store (left stick to vx and vy, right stick x to yaw, speed slider to cadence) and the `mode` store (Walk walks; every other mode holds the stand pose) to `setCommand`, with the same limits as `leika/robot.py` (0.06 m/s forward, 0.03 m/s sideways, 2.0 rad/s turning).
- `app/src/lib/components/SimulationView.svelte`: three.js scene with the Pico URDF model; each animation frame advances the simulation by the elapsed real time (capped at 50 ms, so a background tab does not trigger a burst), then copies the base pose and joint angles onto the model.
  Shows loading progress for the WASM, a Reset button, and a note when the Pico has fallen.
- A "Simulation" entry in the view selector's default views and in the widget registry, gated by `HAS_3D_VIEW`.
- `simulation/scripts/export_gait_trace.py` writes `app/tests/fixtures/pico-gait-trace.json`.

## Data flow

joystick / mode stores -> `controls.ts` -> `pico-sim.setCommand` -> each 10 ms tick: command mapping -> phase advance -> foot targets -> IK -> actuator targets -> 5 MuJoCo steps -> `SimulationView` copies qpos onto the URDF model (MuJoCo Z-up world rotated into the view's Y-up world, as the 3D view already does).

## Error handling

- WASM fails to load (old browser, network): the view shows the error and a Retry button; the rest of the app is unaffected.
- The Pico falls (base height below half the stand height, or tilt beyond 60 degrees): the view says so and offers Reset; the simulation keeps running so the user sees what happened.
- An IK target out of reach: the port keeps the Python behaviour, which clamps the reach and the knee cosine (`firmware_gait.py` `leg_ik`), and the golden trace includes an out-of-reach target.

## Testing

- Golden trace: TypeScript gait and IK against `firmware_gait.py` for stand, walk forward, strafe and turn sequences.
- Physics, headless in Vitest if the MuJoCo WASM loads under Node (checked first; otherwise a Playwright test in a browser): standing for 2 s keeps the base within 10% of the stand height, and walking forward for 3 s moves the base forward.
- Manual: the view in Chromium and Firefox, portrait phone and desktop.

## Open risks

- The golden trace pins the port to the Python controller; it does not show that either matches the real robot, which runs the firmware's MINI kinematics.
- Python pins `mujoco>=3.10`, the browser uses 3.14; physics may differ slightly between versions, which is why only the controller, not the physics, is compared exactly.
- The app's single-bundle build may inline the 293 KB loader into the main bundle; the implementation measures this and moves the loader to a separate fetch if it does.
