# Robot selection in the simulation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Simulation view simulates the Pico, Spot Micro or Yertle, each driven by a TypeScript port of the firmware motion code pinned to the real headers, with the Pico's training controller kept as a second choice.

**Architecture:** A host harness compiles the real firmware motion headers per kinematics variant and writes golden traces; a TypeScript port of `MotionService`'s loop (rest/stand/walk states, kinematics, `dir` table, 0.1 degree update threshold) must reproduce them. A Python generator turns the Spot Micro and Yertle URDFs into MJCF scenes. The existing `PicoSim` becomes a `RobotSim` fed by robot definitions (scene, meshes, joint mapping, controllers), and the view gains robot and controller choosers.

**Tech Stack:** C++20 (host g++ 13, MSYS2 UCRT64 here, plain g++ on Linux), nanopb, Python 3.13 with uv and MuJoCo's `MjSpec`, `xacro`, TypeScript, Vitest, `@mujoco/mujoco` 3.14.

**Spec:** `docs/superpowers/specs/2026-09-29-sim-robot-selection-design.md`

## Global Constraints

- The firmware headers are compiled unmodified; only `esp_log.h` and `dspm_mult.h` are stubbed, in `esp32/test/host/stubs/`.
- Host compile flags: `-std=gnu++20 -D<VARIANT> -Iesp32/test/host/stubs -Iesp32/include -Iesp32/src -Isubmodules/nanopb` (`gnu` because the firmware uses `M_PI`).
- Port tolerance against the host traces: 1e-4 degrees for angles and 1e-6 m for positions (the firmware computes in `float`; the port in `double`).
- Control tick 10 ms and 5 MuJoCo steps per tick, as the existing simulation.
- Joint targets reach MuJoCo in radians; the firmware port outputs the servo-convention degrees `MotionService::update_angles` produces.
- Everything new in the app stays behind `HAS_3D_VIEW`, and every new static file is listed in `esp32/scripts/build_app.py` `MODEL_FILES`.
- Repository rules as before: English, no dead code, pnpm, uv, one-line gitmoji commit messages, one sentence per Markdown line.

## Review Focus

- Switching robot while the previous one is still loading: only the latest choice may end up simulated, and the abandoned load must be freed.
- A saved robot choice that no longer exists (a renamed id in localStorage): the view falls back to the default instead of crashing.
- The robot in Walk with the sticks released: the firmware port must stop the gait (`phase_time` reset) exactly as the firmware does, not creep.
- The IMU compensation path in Stand: the port must take the simulated base tilt, in the firmware's units and sign, or Stand fights the physics.
- Generated MJCF changing silently when the URDF changes: the regeneration test must fail rather than let the committed scene drift.

---

### Task 1: Host harness and firmware traces

**Files:**
- Create: `esp32/test/host/stubs/esp_log.h`, `esp32/test/host/stubs/dspm_mult.h`
- Create: `esp32/test/host/firmware_trace.cpp`
- Create: `esp32/test/host/export_firmware_traces.py` (compiles the trace program per variant with `$CXX` or `g++`, runs it, writes the fixtures)
- Create (generated): `app/tests/fixtures/firmware-trace-SPOTMICRO_ESP32.json`, `-SPOTMICRO_ESP32_MINI.json`, `-SPOTMICRO_YERTLE.json`
- Modify: `app/.prettierignore` (the three fixtures)

**Interfaces:**
- Produces, per fixture: `variant`, `kin` (`coxa, coxa_offset, femur, tibia, L, W` plus the derived constants the port needs), `dt`, and `ticks[]` of `{ step, mode, gait, cmd: {lx, ly, rx, ry, h, s, s1}, imu: [angleX, angleY], body: {omega, phi, psi, xm, ym, zm, feet: 4x3}, angles: 12 }`.
  `angles` are `MotionService`'s servo-convention output: IK degrees times `dir`, updated only when they change by more than 0.1 degrees.

- [ ] **Step 1:** Write the two stubs (`ESP_LOGI` as a no-op; `esp_err_t` and `ESP_OK` for `dspm_mult.h`, which the firmware's `math_utils.h` includes but whose multiply the motion code never calls).
- [ ] **Step 2:** Write `firmware_trace.cpp`: it owns `RestState`, `StandState`, `WalkState`, `Kinematics`, `body_state_t` and `CommandMsg` and reproduces `MotionService::update` and `update_angles` (`dir = {1,-1,-1,-1,-1,-1,1,-1,-1,-1,-1,-1}`, threshold 0.1).
  Script (dt 0.01): rest 50 ticks; stand 100 ticks with `h=0.6, lx=0.3, ly=-0.2, rx=0.4, ry=-0.3` and IMU `(0.02, -0.03)` rad; walk trot 200 ticks with `ly=1, s=0.5, s1=0.5, h=0.5`; strafe `lx=-1` 100; turn `rx=1` 100; sticks released 50; crawl gait `ly=0.6` 150; stand 50.
  It prints one JSON document to stdout.
- [ ] **Step 3:** Write `export_firmware_traces.py` (no third-party imports) and run: `cd esp32/test/host && python export_firmware_traces.py` with `CXX=/c/msys64/ucrt64/bin/g++` here.
  Expected: three fixtures written, each with 800 ticks.
- [ ] **Step 4:** Commit: `✅ Traces the firmware motion code on the host for each kinematics variant`.

### Task 2: TypeScript port of the firmware motion

**Files:**
- Create: `app/src/lib/simulation/firmware/kin-config.ts` (per-variant `KinConfig`, derived constants as in `kinematics.h`)
- Create: `app/src/lib/simulation/firmware/kinematics.ts` (`calculateInverseKinematics`, `legIk`, with the Yertle tibia variant)
- Create: `app/src/lib/simulation/firmware/states.ts` (`RestState`, `StandState`, `WalkState` including crawl, body shift and smoothing)
- Create: `app/src/lib/simulation/firmware/motion.ts` (`FirmwareMotion`: `setMode(ModesEnum)`, `setGait(WalkGaits)`, `handleInput(ControllerData)`, `update(dt, imu: [angleX, angleY]): number[]` returning servo-convention degrees)
- Test: `app/tests/unit/firmware-motion.spec.ts`

**Interfaces:**
- Consumes: the three fixtures.
- Produces: `type Variant = 'SPOTMICRO_ESP32' | 'SPOTMICRO_ESP32_MINI' | 'SPOTMICRO_YERTLE'`, `new FirmwareMotion(variant)`, and the methods above.

- [ ] **Step 1:** Write the test: for each fixture, replay the ticks through `FirmwareMotion` (calling `setMode`/`setGait`/`handleInput` when the fixture's mode, gait or command changes, as the firmware's message handlers are called), and compare body state and angles per tick within the Global Constraints tolerances, naming the tick and step in each failure.
- [ ] **Step 2:** Run it; expected FAIL on the missing module.
- [ ] **Step 3:** Port the headers line by line, keeping their names so a reader can diff them.
- [ ] **Step 4:** Run it; expected 3 variants pass. Mutation-check one sign and one constant.
- [ ] **Step 5:** Commit: `✨ Ports the firmware motion code to TypeScript, pinned to the headers per variant`.

### Task 3: MJCF scenes for Spot Micro and Yertle

**Files:**
- Modify: `simulation/pyproject.toml`, `simulation/uv.lock` (`xacro`)
- Create: `simulation/generate_scenes.py`
- Create (generated, committed): `simulation/src/resources/spot_micro/scene.xml` and `meshes/`, `simulation/src/resources/yertle/scene.xml` and `meshes/`
- Test: `simulation/tests/test_generated_scenes.py`

**Interfaces:**
- Produces: MJCF scenes with a `floor` geom, a `root` free joint on the base body, a foot sphere geom and `foot_<leg>` site at each toe frame, one position actuator per revolute joint named after the joint, and an `imu` site on the base; meshes in `meshes/` beside the scene.

- [ ] **Step 1:** Write the test: regenerating into a temporary directory yields files byte-identical to the committed ones; each scene loads in MuJoCo with 12 actuators, a free joint and four foot sites.
- [ ] **Step 2:** Run it (`cd simulation && uv run pytest tests/test_generated_scenes.py`); expected FAIL (no generator).
- [ ] **Step 3:** Write the generator: expand the xacro, rewrite `package://` mesh paths to the app's static meshes, load with `mujoco.MjSpec` (keeping static bodies, so the toe frames survive), add floor, free joint, foot spheres, actuators (kp and force range per robot, recorded as approximations in the file header) and IMU site, and write the scene and meshes.
- [ ] **Step 4:** Run the test; expected PASS. Then check in Python that each robot, holding its firmware stand angles, stays up for 2 s; tune the approximations until it does and record them.
- [ ] **Step 5:** Commit: `✨ Generates MuJoCo scenes for Spot Micro and Yertle from their URDFs`.

### Task 4: One simulation for every robot

**Files:**
- Rename: `app/src/lib/simulation/pico-sim.ts` to `robot-sim.ts` (`RobotSim`), and its test to `robot-sim.spec.ts`
- Create: `app/src/lib/simulation/robots.ts` (robot definitions and controllers)
- Modify: `app/src/lib/simulation/controls.ts` (becomes the training controller's adapter)

**Interfaces:**
- Produces:
  - `interface SimController { reset(): void; tick(controls: SimControls): Record<string, number> }` (joint name to radians), with `SimControls = { input: ControllerData; mode: ModesEnum; gait: WalkGaits; imu: [number, number] }`
  - `interface RobotDefinition { id: 'pico' | 'spot_micro' | 'yertle'; label: string; variant: Variant; scene: string; meshZip: string; controllers: { id: string; label: string; create(assets): SimController }[] }`
  - `ROBOTS: RobotDefinition[]`, `robotById(id): RobotDefinition | undefined`
  - `new RobotSim(mujoco, assets, controller)` with the existing `reset/step/basePosition/baseQuaternion/jointAngles/hasFallen/time/dispose`, and `imu()` returning the firmware's `angleX, angleY` from the base orientation.

- [ ] **Step 1:** Extend the physics test to every robot and controller: stands 2 s, walks forward 3 s (for Spot Micro and Yertle, forward is their +X), no fall.
- [ ] **Step 2:** Run; expected FAIL.
- [ ] **Step 3:** Generalise `PicoSim`, write the firmware controller (firmware servo degrees to joint radians through each robot's order, sign and offset table) and the training controller adapter; derive the Pico's firmware offsets from the firmware's default-stance FK and check them against the MJCF foot sites.
  If the Pico cannot stand on the firmware controller, drop that controller and ledger it, as the spec allows.
- [ ] **Step 4:** Run; expected PASS for every robot and controller that remains.
- [ ] **Step 5:** Commit: `✨ Simulates any robot through one simulation fed by robot definitions`.

### Task 5: Assets and choosers in the view

**Files:**
- Modify: `app/scripts/build_pico_model.js` (publish every robot's scene and mesh zip; rename to `build_sim_models.js` and update `package.json`)
- Modify: `app/.gitignore`, `esp32/scripts/build_app.py` `MODEL_FILES`
- Modify: `app/src/lib/simulation/load.ts` (load a given robot's assets)
- Modify: `app/src/lib/components/SimulationView.svelte` (robot and controller choosers, persisted; the model drawn is the chosen robot's; loads guarded so only the latest choice lands)
- Test: `app/tests/unit/simulation-view.spec.ts` (a switch during a load frees the abandoned simulation; an unknown saved id falls back to the default)

- [ ] **Step 1:** Write the two view tests; run; expected FAIL.
- [ ] **Step 2:** Implement; run; expected PASS.
- [ ] **Step 3:** Browser check: each robot stands and walks in dev and in the production preview; reload keeps the choice.
- [ ] **Step 4:** Commit: `✨ Lets the Simulation view switch between the Pico, Spot Micro and Yertle`.

### Task 6: Gate

- [ ] **Step 1:** App: tests, check, lint, prettier. Simulation: `uv run pytest`. Firmware: `pio run -e esp32-camera`, flash use unchanged, no new files embedded.
