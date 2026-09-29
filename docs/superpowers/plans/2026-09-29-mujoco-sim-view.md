# MuJoCo simulation view Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A "Simulation" view in the hosted web app where a MuJoCo-simulated Pico stands, walks and turns under the app's joystick and mode controls, using the same gait and IK as the Python simulation.

**Architecture:** A pure TypeScript port of `simulation/src/robot/firmware_gait.py`, pinned to the Python original by a golden trace, drives MuJoCo's official WASM bindings, which load the simulation's own `scene.xml` and meshes. A Svelte view renders the result on the existing Pico URDF model and is loaded lazily, behind `HAS_3D_VIEW`, so neither the firmware's app nor the rest of the web app grows.

**Tech Stack:** `@mujoco/mujoco` 3.14.0 (single-threaded WASM), SvelteKit 2 / Svelte 5, three.js with urdf-loader, Vitest, Python 3.13 with uv for the trace exporter.

**Spec:** `docs/superpowers/specs/2026-09-29-mujoco-sim-view-design.md`

## Global Constraints

- Hosted app only: everything new is reached through `HAS_3D_VIEW` (`app/src/lib/build-flags.ts`), so `PUBLIC_EMBEDDED_BUILD=true` builds contain none of it.
- Single-threaded MuJoCo build only (`@mujoco/mujoco`, not `@mujoco/mujoco/mt`): GitHub Pages cannot send COOP/COEP headers.
- Control at 100 Hz (`CONTROL_DT = 0.01`), 5 physics steps of 0.002 s per tick, as `simulation/src/sim/mj_runtime.py`.
- Command limits as `simulation/src/leika/robot.py`: 0.06 m/s forward, 0.03 m/s sideways, 2.0 rad/s turning.
- Joystick signs as the firmware (`esp32/include/motion_states/walk_state.h:112-115`): forward from `left.y`, sideways from `-left.x`, turn from `right.x`.
- The simulation never writes to the socket.
- One copy of the robot: model, scene, meshes and gait coefficients come from `simulation/src/resources/spot_pico` through `pnpm model`.
- Repository rules: English only, no dead code, pnpm, uv for Python, commit messages are one line `[gitmoji] [Verb] ...` with no body or trailers, each Markdown sentence on its own line.

## Review Focus

- A background tab returning after minutes: the frame loop must advance at most 50 ms of simulation per animation frame, not replay minutes of physics at once.
- Opening the Simulation view twice (leave and come back): the second visit must build a fresh model without leaking the first one's `MjModel`/`MjData` or its animation frame.
- Joystick at the edge (magnitude above 1 from a gamepad or diagonal keys): commands must clamp to the limits, not exceed them.
- A connected robot of another variant (Spot Micro, Yertle): the view still simulates the Pico and says so, instead of loading a model it cannot simulate.
- The WASM failing to download (offline, blocked): the view shows the error with Retry and the rest of the app keeps working.

---

## File Structure

- `simulation/export_gait_trace.py` (create): writes the golden trace from `firmware_gait.py`.
- `app/tests/fixtures/pico-gait-trace.json` (create, generated): the trace.
- `app/src/lib/simulation/pico-gait.ts` (create): gait engine, IK and command mapping, pure.
- `app/tests/unit/pico-gait.spec.ts` (create): port against the trace.
- `app/scripts/build_pico_model.js` (modify): also writes `static/spot_pico_scene.xml` and `static/spot_pico_gait.json`.
- `app/.gitignore`, `esp32/scripts/build_app.py` (modify): ignore and exclude those two files.
- `app/src/lib/simulation/pico-sim.ts` (create): MuJoCo model, data, stepping and read-outs.
- `app/tests/unit/pico-sim.spec.ts` (create): headless physics checks in the Node environment.
- `app/src/lib/simulation/controls.ts` (create): stores to simulation command.
- `app/tests/unit/sim-controls.spec.ts` (create).
- `app/src/lib/simulation/load.ts` (create): loads the WASM and the static assets in the browser.
- `app/src/lib/components/SimulationView.svelte` (create): renders the simulation on the Pico URDF model.
- `app/src/lib/components/LazySimulationView.svelte` (create): lazy, `HAS_3D_VIEW`-gated wrapper.
- `app/src/lib/stores/widget-components.ts`, `app/src/lib/stores/application.ts` (modify): register the widget and the "Simulation" view.

---

### Task 1: Golden trace from the Python controller

**Files:**
- Create: `simulation/export_gait_trace.py`
- Create (generated): `app/tests/fixtures/pico-gait-trace.json`

**Interfaces:**
- Produces: `pico-gait-trace.json` with keys `control_dt`, `gait_coef`, `default_feet` (4x3), `stand_z`, `ticks` (array of `{segment, cmd: [vx, vy, yaw], phase, feet: 4x3, angles: 12}`), `pose` (`{body: {omega, phi, psi, xm, ym, zm}, angles: 12}`), `unreachable` (`{leg, target: 3, angles: 3}`).

- [ ] **Step 1: Write the exporter**

```python
"""Exports a golden trace of firmware_gait.py for the web app's TypeScript port.

The app's app/src/lib/simulation/pico-gait.ts must reproduce this trace, so the browser
simulation and this Python simulation run the same controller.
Run from simulation/: uv run python export_gait_trace.py
"""
import json
import os

import numpy as np

from src.robot.firmware_gait import (
    DEFAULT_FEET,
    GAIT_COEF,
    STAND_Z,
    BodyState,
    GaitController,
    GaitState,
    Kinematics,
    SpotPicoKinConfig,
    analytic_gait_action,
    leg_ik,
)

CONTROL_DT = 0.01
# (name, [vx, vy, yaw], ticks): stand, then each command direction, then a mixed one.
SEGMENTS = [
    ("stand", [0.0, 0.0, 0.0], 50),
    ("forward", [0.06, 0.0, 0.0], 150),
    ("strafe", [0.0, 0.03, 0.0], 100),
    ("turn", [0.0, 0.0, 2.0], 100),
    ("mixed", [0.04, -0.02, 1.0], 100),
]
POSE = dict(omega=0.1, phi=-0.05, psi=0.08, xm=0.005, ym=-0.004, zm=0.01)
UNREACHABLE = ("fr", [-0.2, -0.1, -0.3])
OUTPUT = os.path.join(os.path.dirname(__file__), "..", "app", "tests", "fixtures", "pico-gait-trace.json")


def main() -> None:
    gait, controller, body, kinematics = GaitState(), GaitController(), BodyState(), Kinematics()
    ticks = []
    for name, cmd, count in SEGMENTS:
        for _ in range(count):
            analytic_gait_action(cmd, gait)
            controller.advance_phase(gait, CONTROL_DT)
            controller.generate_feet(gait, body)
            ticks.append({
                "segment": name,
                "cmd": cmd,
                "phase": controller.phase,
                "feet": body.feet.tolist(),
                "angles": kinematics.inverse_kinematics(body).tolist(),
            })

    posed = BodyState(**POSE)
    leg, target = UNREACHABLE
    trace = {
        "control_dt": CONTROL_DT,
        "gait_coef": GAIT_COEF,
        "default_feet": DEFAULT_FEET.tolist(),
        "stand_z": STAND_Z,
        "ticks": ticks,
        "pose": {"body": POSE, "angles": kinematics.inverse_kinematics(posed).tolist()},
        "unreachable": {
            "leg": leg,
            "target": target,
            "angles": leg_ik(SpotPicoKinConfig(), leg, np.array(target)).tolist(),
        },
    }
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w") as f:
        json.dump(trace, f)
    print(f"Wrote {len(ticks)} ticks to {os.path.normpath(OUTPUT)}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it**

Run: `cd simulation && uv run python export_gait_trace.py`
Expected: `Wrote 500 ticks to ...app\tests\fixtures\pico-gait-trace.json`, and the file exists.

- [ ] **Step 3: Commit**

```bash
git add simulation/export_gait_trace.py app/tests/fixtures/pico-gait-trace.json
git commit -m "✅ Exports a golden trace of the Pico gait for the web app's port"
```

### Task 2: TypeScript port of the gait and IK

**Files:**
- Create: `app/src/lib/simulation/pico-gait.ts`
- Test: `app/tests/unit/pico-gait.spec.ts`

**Interfaces:**
- Consumes: the fixture from Task 1.
- Produces:
  - `type Vec3 = [number, number, number]`, `type Command = [vx: number, vy: number, yaw: number]`
  - `interface GaitCoef { gain_x; gain_y; gain_yaw; speed_base; speed_slope; step_height; step_depth: number }`
  - `DEFAULT_FEET: Vec3[]`, `STAND_Z: number`, `CONTROL_DT = 0.01`, `JOINT_NAMES: string[]` (12, MJCF order)
  - `class BodyState { omega; phi; psi; xm; ym; zm: number; feet: Vec3[] }` (constructor takes a partial pose)
  - `class GaitState`, `class GaitController { phase; advancePhase(gait, dt); generateFeet(gait, body) }`
  - `inverseKinematics(body: BodyState): number[]` (12 radians), `legIk(leg: Leg, target: Vec3): Vec3`
  - `analyticGaitAction(cmd: Command, gait: GaitState, coef: GaitCoef): void`
  - `class PicoController { constructor(coef: GaitCoef); tick(cmd: Command): number[] }` (one 10 ms tick: command mapping, phase advance, feet, IK)

- [ ] **Step 1: Write the failing test**

```ts
import { describe, it, expect } from 'vitest'
import trace from '../fixtures/pico-gait-trace.json'
import {
    BodyState,
    DEFAULT_FEET,
    PicoController,
    STAND_Z,
    inverseKinematics,
    legIk,
    type Command,
    type GaitCoef,
    type Vec3
} from '../../src/lib/simulation/pico-gait'

// The trace comes from simulation/export_gait_trace.py; regenerate it there when the Python
// controller changes, and this port must follow.
const TOLERANCE = 1e-9
const maxDifference = (a: number[], b: number[]) =>
    Math.max(...a.map((value, i) => Math.abs(value - b[i])))

describe('Pico gait port', () => {
    it('derives the same stance as the Python controller', () => {
        expect(maxDifference(DEFAULT_FEET.flat(), trace.default_feet.flat())).toBeLessThan(TOLERANCE)
        expect(Math.abs(STAND_Z - trace.stand_z)).toBeLessThan(TOLERANCE)
    })

    it('reproduces every tick of the Python gait: feet and joint targets', () => {
        const controller = new PicoController(trace.gait_coef as GaitCoef)
        for (const [index, tick] of trace.ticks.entries()) {
            const angles = controller.tick(tick.cmd as Command)
            const where = `tick ${index} (${tick.segment})`
            expect(Math.abs(controller.phase - tick.phase), where).toBeLessThan(TOLERANCE)
            expect(maxDifference(controller.body.feet.flat(), tick.feet.flat()), where).toBeLessThan(TOLERANCE)
            expect(maxDifference(angles, tick.angles), where).toBeLessThan(TOLERANCE)
        }
    })

    it('shifts the feet for a body pose as the Python IK does', () => {
        const angles = inverseKinematics(new BodyState(trace.pose.body))
        expect(maxDifference(angles, trace.pose.angles)).toBeLessThan(TOLERANCE)
    })

    it('clamps an unreachable target the same way', () => {
        const { leg, target, angles } = trace.unreachable
        expect(maxDifference(legIk(leg as 'fr', target as Vec3), angles)).toBeLessThan(TOLERANCE)
    })
})
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cd app && pnpm exec vitest run tests/unit/pico-gait.spec.ts`
Expected: FAIL, `Failed to resolve import "../../src/lib/simulation/pico-gait"`.

- [ ] **Step 3: Write the port**

```ts
/**
 * TypeScript port of simulation/src/robot/firmware_gait.py: the firmware's walk_state.h gait
 * engine with inverse kinematics derived from the Pico's MJCF, which is the controller the
 * Python simulation and the training use. tests/unit/pico-gait.spec.ts pins it to that file.
 *
 * Base frame of spot_pico: +X left, +Y rear, +Z up (forward is -Y). Angles are radians in MJCF
 * order [fr, fl, rr, rl] x [hip, femur, tibia].
 */

export type Vec3 = [number, number, number]
export type Command = [vx: number, vy: number, yaw: number]
export type Leg = 'fr' | 'fl' | 'rr' | 'rl'

export const LEG_NAMES: Leg[] = ['fr', 'fl', 'rr', 'rl']
export const JOINT_NAMES = LEG_NAMES.flatMap(leg =>
    ['hip', 'femur', 'tibia'].map(joint => `${leg}_${joint}_joint`)
)
export const CONTROL_DT = 0.01

interface LegGeometry {
    H: Vec3
    sy: number
    pFemur: Vec3
    sf: number
    pTibia: Vec3
    st: number
    pFoot: Vec3
}

// From simulation/src/resources/spot_pico/scene.xml, as _LEG_GEOM in firmware_gait.py.
const LEG_GEOM: Record<Leg, LegGeometry> = {
    fr: {
        H: [-0.039922, -0.099805, 0.000058],
        sy: -1,
        pFemur: [-0.0513, 0.004305, 0],
        sf: -1,
        pTibia: [0.00215, 0.043903, -0.037783],
        st: -1,
        pFoot: [0.006, -0.037123, -0.037123]
    },
    fl: {
        H: [0.040078, -0.095155, 0.000058],
        sy: 1,
        pFemur: [0.0516, 0.004305, 0],
        sf: -1,
        pTibia: [-0.0143, 0.043903, -0.037783],
        st: 1,
        pFoot: [0.006, -0.037123, -0.037123]
    },
    rr: {
        H: [-0.039922, 0.077545, 0.000058],
        sy: -1,
        pFemur: [-0.0513, 0.004305, 0],
        sf: -1,
        pTibia: [0.00215, 0.043903, -0.037783],
        st: -1,
        pFoot: [0.006, -0.037123, -0.037123]
    },
    rl: {
        H: [0.040078, 0.077545, 0.000058],
        sy: 1,
        pFemur: [0.0516, 0.004305, 0],
        sf: -1,
        pTibia: [-0.0143, 0.043903, -0.037783],
        st: 1,
        pFoot: [0.006, -0.037123, -0.037123]
    }
}

const STANCE_DEPTH = 0.055
const FOOT_RADIUS = 0.009

const rotX = (a: number, [x, y, z]: Vec3): Vec3 => {
    const c = Math.cos(a)
    const s = Math.sin(a)
    return [x, c * y - s * z, s * y + c * z]
}
const rotY = (a: number, [x, y, z]: Vec3): Vec3 => {
    const c = Math.cos(a)
    const s = Math.sin(a)
    return [c * x + s * z, y, -s * x + c * z]
}
const rotZ = (a: number, [x, y, z]: Vec3): Vec3 => {
    const c = Math.cos(a)
    const s = Math.sin(a)
    return [c * x - s * y, s * x + c * y, z]
}
const clip = (value: number, low: number, high: number) => Math.min(Math.max(value, low), high)

// Per-leg constants for the analytic IK, as SpotPicoKinConfig.
const KIN = Object.fromEntries(
    LEG_NAMES.map(leg => {
        const { pFemur, pTibia, pFoot } = LEG_GEOM[leg]
        const v1 = [pTibia[1], pTibia[2]]
        const v2 = [pFoot[1], pFoot[2]]
        return [
            leg,
            {
                xOff: pFemur[0] + pTibia[0] + pFoot[0],
                o0: [pFemur[1], pFemur[2]],
                L1: Math.hypot(v1[0], v1[1]),
                L2: Math.hypot(v2[0], v2[1]),
                a1: Math.atan2(v1[1], v1[0]),
                a2: Math.atan2(v2[1], v2[0])
            }
        ]
    })
) as Record<Leg, { xOff: number; o0: number[]; L1: number; L2: number; a1: number; a2: number }>

/** Base-frame foot target to (hip, femur, tibia) radians; the leg reaches downward, knee back. */
export function legIk(leg: Leg, target: Vec3): Vec3 {
    const { H, sy, sf, st } = LEG_GEOM[leg]
    const { xOff, o0, L1, L2, a1, a2 } = KIN[leg]

    const P = [target[0] - H[0], target[1] - H[1], target[2] - H[2]]
    const Ay = P[1]
    const r2 = P[0] ** 2 + P[2] ** 2
    const Az = -Math.sqrt(Math.max(r2 - xOff ** 2, 0))

    const denom = r2 > 1e-12 ? r2 : 1e-12
    const c = (xOff * P[0] + Az * P[2]) / denom
    const s = (Az * P[0] - xOff * P[2]) / denom
    const q1 = Math.atan2(s, c) / sy

    const T = [Ay - o0[0], Az - o0[1]]
    const D = Math.min(Math.hypot(T[0], T[1]), L1 + L2 - 1e-6)
    const cosPsi = clip((D * D - L1 * L1 - L2 * L2) / (2 * L1 * L2), -1, 1)
    const psi = -Math.acos(cosPsi)
    const alpha =
        Math.atan2(T[1], T[0]) - Math.atan2(L2 * Math.sin(psi), L1 + L2 * Math.cos(psi)) - a1
    const beta = psi - (a2 - a1)
    return [q1, alpha / sf, beta / st]
}

export const DEFAULT_FEET: Vec3[] = LEG_NAMES.map(leg => {
    const { H } = LEG_GEOM[leg]
    return [H[0] + KIN[leg].xOff, H[1], H[2] - STANCE_DEPTH]
})
export const STAND_Z = FOOT_RADIUS - Math.min(...DEFAULT_FEET.map(foot => foot[2]))

const TROT_OFFSET = [0, 0.5, 0.5, 0]
const TROT_STAND_FRAC = 0.75
const TROT_SPEED_FACTOR = 2.0
const COMBINATORIAL_VALUES = [1, 11, 55, 165, 330, 462, 462, 330, 165, 55, 11, 1]
const BEZIER_STEPS = [-1.0, -1.4, -1.5, -1.5, -1.5, 0.0, 0.0, 0.0, 1.5, 1.5, 1.4, 1.0]
const BEZIER_HEIGHTS = [0.0, 0.0, 0.9, 0.9, 0.9, 0.9, 0.9, 1.1, 1.1, 1.1, 0.0, 0.0]
const MAX_STEP_LENGTH = 0.03
const MAX_LATERAL_STEP = 0.018
const MAX_TURN_STEP = 0.026
const TURN_RATE =
    (2 * MAX_TURN_STEP) /
    (DEFAULT_FEET.reduce((sum, [x, y]) => sum + Math.hypot(x, y), 0) / DEFAULT_FEET.length)
const DEFAULT_STEP_HEIGHT = 0.015
const DEFAULT_STEP_DEPTH = 0.002

type Pose = { omega: number; phi: number; psi: number; xm: number; ym: number; zm: number }

export class BodyState {
    omega = 0
    phi = 0
    psi = 0
    xm = 0
    ym = 0
    zm = 0
    feet: Vec3[] = DEFAULT_FEET.map(foot => [...foot] as Vec3)

    constructor(pose: Partial<Pose> = {}) {
        Object.assign(this, pose)
    }
}

export class GaitState {
    stepHeight = DEFAULT_STEP_HEIGHT
    stepX = 0
    stepZ = 0
    stepAngle = 0
    stepVelocity = 0.5
    stepDepth = DEFAULT_STEP_DEPTH
    standFrac = TROT_STAND_FRAC
    offset = [...TROT_OFFSET]
    speedFactor = TROT_SPEED_FACTOR
}

const stanceCurve = (length: number, angle: number, depth: number, phase: number, point: Vec3) => {
    const step = length * (1 - 2 * phase)
    point[0] += step * Math.cos(angle)
    point[2] += step * Math.sin(angle)
    if (length !== 0) point[1] = -depth * Math.cos((Math.PI * (point[0] + point[2])) / (2 * length))
}

const bezierCurve = (length: number, angle: number, height: number, phase: number, point: Vec3) => {
    const xPolar = Math.cos(angle)
    const zPolar = Math.sin(angle)
    const t = Math.min(Math.max(phase, 1e-4), 1 - 1e-4)
    const oneMinus = 1 - t
    let phasePower = 1
    let invPhasePower = oneMinus ** 11
    for (let i = 0; i < 12; i++) {
        const b = COMBINATORIAL_VALUES[i] * phasePower * invPhasePower
        point[0] += b * BEZIER_STEPS[i] * length * xPolar
        point[2] += b * BEZIER_STEPS[i] * length * zPolar
        point[1] += b * BEZIER_HEIGHTS[i] * height
        phasePower *= t
        if (oneMinus !== 0) invPhasePower /= oneMinus
    }
}

/** Translation and rotation composed into one stride vector at a foot: (amplitude, heading). */
const stroke = (gait: GaitState, foot: Vec3): [number, number] => {
    const turn = gait.stepAngle * TURN_RATE
    const sx = gait.stepZ + turn * foot[1]
    const sy = -gait.stepX - turn * foot[0]
    return [Math.hypot(sx, sy), Math.atan2(sx, -sy)]
}

export class GaitController {
    phase = 0
    private readonly defaultPosition = DEFAULT_FEET

    advancePhase(gait: GaitState, dt: number) {
        const velocity = Math.max(gait.stepVelocity, 0.5)
        this.phase = (this.phase + dt * velocity * gait.speedFactor) % 1
    }

    /** Writes body.feet at the current phase, in the base frame. */
    generateFeet(gait: GaitState, body: BodyState) {
        const moving =
            Math.abs(gait.stepX) > 1e-6 || Math.abs(gait.stepZ) > 1e-6 || Math.abs(gait.stepAngle) > 1e-6
        body.feet = this.defaultPosition.map((home, i) => {
            const legPhase = (this.phase + gait.offset[i]) % 1
            const contact = legPhase <= gait.standFrac
            const phase = contact ? legPhase / gait.standFrac : (legPhase - gait.standFrac) / (1 - gait.standFrac)
            const [amplitude, heading] = stroke(gait, home)
            const delta: Vec3 = [0, 0, 0]
            if (contact) stanceCurve(amplitude * 0.5, heading, gait.stepDepth, phase, delta)
            else bezierCurve(amplitude * 0.5, heading, gait.stepHeight, phase, delta)
            // Stride frame to base frame: forward stride to -Y, cross-stride to +X, lift to +Z.
            return moving ?
                    [home[0] + delta[2], home[1] - delta[0], home[2] + delta[1]]
                :   [home[0] + delta[2], home[1], home[2]]
        })
    }
}

/** 12 joint angles for the feet, after shifting them by the body pose (identity by default). */
export function inverseKinematics(body: BodyState): number[] {
    return LEG_NAMES.flatMap((leg, i) => {
        const foot = body.feet[i]
        const shifted: Vec3 = [foot[0] - body.xm, foot[1] - body.ym, foot[2] - body.zm]
        const local = rotX(-body.omega, rotY(-body.phi, rotZ(-body.psi, shifted)))
        return legIk(leg, local)
    })
}

export interface GaitCoef {
    gain_x: number
    gain_y: number
    gain_yaw: number
    speed_base: number
    speed_slope: number
    step_height: number
    step_depth: number
}

/** Command [vx, vy, yaw] (m/s, m/s, rad/s) to gait parameters, as analytic_gait_action. */
export function analyticGaitAction([vx, vy, yaw]: Command, gait: GaitState, coef: GaitCoef) {
    gait.stepX = clip(vx / coef.gain_x, -1, 1) * MAX_STEP_LENGTH
    gait.stepZ = clip(vy / coef.gain_y, -1, 1) * MAX_LATERAL_STEP
    gait.stepAngle = clip(-yaw / coef.gain_yaw, -1, 1)
    const speed = Math.hypot(vx, vy) + Math.abs(yaw) * 0.1
    gait.stepVelocity = clip(coef.speed_base + coef.speed_slope * speed, 0, 1)
    gait.stepHeight = coef.step_height
    gait.stepDepth = coef.step_depth
}

/** One controller instance per simulated robot: the loop body of leika/robot.py. */
export class PicoController {
    readonly gait = new GaitState()
    readonly body = new BodyState()
    private readonly gaitController = new GaitController()

    constructor(private readonly coef: GaitCoef) {}

    get phase() {
        return this.gaitController.phase
    }

    /** One 10 ms control tick; returns the 12 actuator targets in radians. */
    tick(cmd: Command): number[] {
        analyticGaitAction(cmd, this.gait, this.coef)
        this.gaitController.advancePhase(this.gait, CONTROL_DT)
        this.gaitController.generateFeet(this.gait, this.body)
        return inverseKinematics(this.body)
    }
}
```

- [ ] **Step 4: Run it to verify it passes**

Run: `cd app && pnpm exec vitest run tests/unit/pico-gait.spec.ts`
Expected: 4 passed.
If a tick differs, the message names the tick and segment; compare that function line by line with `firmware_gait.py` rather than loosening the tolerance.

- [ ] **Step 5: Commit**

```bash
git add app/src/lib/simulation/pico-gait.ts app/tests/unit/pico-gait.spec.ts
git commit -m "✨ Ports the Pico gait and IK to TypeScript, pinned to the Python simulation"
```

### Task 3: Simulation assets from `pnpm model`

**Files:**
- Modify: `app/scripts/build_pico_model.js`
- Modify: `app/.gitignore`
- Modify: `esp32/scripts/build_app.py` (`MODEL_FILES`)

**Interfaces:**
- Produces: `static/spot_pico_scene.xml` (the simulation's `scene.xml` verbatim, meshes expected in `meshes/` next to it) and `static/spot_pico_gait.json` (`gait_coef.json` verbatim), besides the existing `spot_pico.urdf` and `spot_pico.zip` (entries `spot_pico/<name>.stl`).

- [ ] **Step 1: Extend the build script**

In `app/scripts/build_pico_model.js`, after the line that writes `spot_pico.urdf`, add:

```js
// The browser simulation loads the simulation's own scene and gait tuning (lib/simulation).
writeFileSync(path.join(staticDir, 'spot_pico_scene.xml'), scene)
writeFileSync(
    path.join(staticDir, 'spot_pico_gait.json'),
    readFileSync(path.join(resources, 'gait_coef.json'))
)
```

- [ ] **Step 2: Ignore and exclude the new files**

Append to `app/.gitignore`, under the existing Pico comment:

```
/static/spot_pico_scene.xml
/static/spot_pico_gait.json
```

In `esp32/scripts/build_app.py`, extend `MODEL_FILES` with `"spot_pico_scene.xml", "spot_pico_gait.json"`.

- [ ] **Step 3: Run it**

Run: `cd app && pnpm model && ls static/spot_pico_* && git status --short static`
Expected: four `spot_pico*` files listed, and `git status` prints nothing for `static`.

- [ ] **Step 4: Commit**

```bash
git add app/scripts/build_pico_model.js app/.gitignore esp32/scripts/build_app.py
git commit -m "🔧 Publishes the Pico scene and gait tuning for the browser simulation"
```

### Task 4: MuJoCo wrapper with headless physics tests

**Files:**
- Modify: `app/package.json` (dependency `@mujoco/mujoco` `3.14.0`, exact)
- Create: `app/src/lib/simulation/pico-sim.ts`
- Test: `app/tests/unit/pico-sim.spec.ts`

**Interfaces:**
- Consumes: `PicoController`, `CONTROL_DT`, `JOINT_NAMES`, `STAND_Z`, `GaitCoef`, `Command` from Task 2; the assets of Task 3.
- Produces:
  - `interface SimAssets { sceneXml: string; meshes: Record<string, Uint8Array>; gaitCoef: GaitCoef }` (mesh keys are bare file names such as `base_link.stl`)
  - `class PicoSim { constructor(mujoco: MainModule, assets: SimAssets); reset(): void; step(seconds: number): void; setCommand(cmd: Command): void; basePosition(): Vec3; baseQuaternion(): [w, x, y, z]; jointAngles(): Record<string, number>; hasFallen(): boolean; dispose(): void }`
  - `MAX_FRAME_SECONDS = 0.05`

- [ ] **Step 1: Add the dependency**

Run: `cd app && pnpm add @mujoco/mujoco@3.14.0`
Expected: `package.json` lists `"@mujoco/mujoco": "3.14.0"` under dependencies.

- [ ] **Step 2: Write the failing test**

```ts
// @vitest-environment node
// MuJoCo's loader reads its WASM from disk under Node; jsdom would make it try fetch().
import { describe, it, expect, beforeAll, afterEach } from 'vitest'
import { readFileSync, readdirSync } from 'node:fs'
import path from 'node:path'
import loadMujoco, { type MainModule } from '@mujoco/mujoco'
import { PicoSim, type SimAssets } from '../../src/lib/simulation/pico-sim'
import { STAND_Z } from '../../src/lib/simulation/pico-gait'

const resources = path.resolve(__dirname, '../../../simulation/src/resources/spot_pico')
const assets: SimAssets = {
    sceneXml: readFileSync(path.join(resources, 'scene.xml'), 'utf8'),
    meshes: Object.fromEntries(
        readdirSync(path.join(resources, 'meshes')).map(name => [
            name,
            new Uint8Array(readFileSync(path.join(resources, 'meshes', name)))
        ])
    ),
    gaitCoef: JSON.parse(readFileSync(path.join(resources, 'gait_coef.json'), 'utf8'))
}

describe('Pico simulation', () => {
    let mujoco: MainModule
    let sim: PicoSim | undefined

    beforeAll(async () => {
        mujoco = await loadMujoco()
    })

    afterEach(() => sim?.dispose())

    const run = (seconds: number) => {
        for (let t = 0; t < seconds; t += 0.05) sim!.step(0.05)
    }

    it('stands on its own', () => {
        sim = new PicoSim(mujoco, assets)
        run(2)
        expect(Math.abs(sim.basePosition()[2] - STAND_Z) / STAND_Z).toBeLessThan(0.1)
        expect(sim.hasFallen()).toBe(false)
    })

    it('walks forward, which is -Y in the Pico frame', () => {
        sim = new PicoSim(mujoco, assets)
        const start = sim.basePosition()
        sim.setCommand([0.06, 0, 0])
        run(3)
        const end = sim.basePosition()
        expect(start[1] - end[1]).toBeGreaterThan(0.03)
        expect(sim.hasFallen()).toBe(false)
    })

    it('never advances more than one frame budget at a time', () => {
        sim = new PicoSim(mujoco, assets)
        const before = sim.time()
        sim.step(120)
        expect(sim.time() - before).toBeLessThanOrEqual(0.05 + 1e-9)
    })

    it('starts over from standing on reset', () => {
        sim = new PicoSim(mujoco, assets)
        sim.setCommand([0.06, 0, 0])
        run(2)
        sim.reset()
        expect(sim.time()).toBe(0)
        expect(Math.abs(sim.basePosition()[1])).toBeLessThan(1e-9)
    })
})
```

- [ ] **Step 3: Run it to verify it fails**

Run: `cd app && pnpm exec vitest run tests/unit/pico-sim.spec.ts`
Expected: FAIL, `Failed to resolve import "../../src/lib/simulation/pico-sim"`.

- [ ] **Step 4: Write the wrapper**

```ts
import type { MainModule, MjData, MjModel } from '@mujoco/mujoco'
import {
    CONTROL_DT,
    JOINT_NAMES,
    PicoController,
    STAND_Z,
    type Command,
    type GaitCoef,
    type Vec3
} from './pico-gait'

export interface SimAssets {
    sceneXml: string
    /** Mesh files by bare name, e.g. base_link.stl, as scene.xml's meshdir="meshes/" expects. */
    meshes: Record<string, Uint8Array>
    gaitCoef: GaitCoef
}

/** The most simulated time one call may advance, so a tab returning from the background resumes instead of replaying. */
export const MAX_FRAME_SECONDS = 0.05
const SCENE_DIR = '/spot_pico'
const JOINT_OBJECT = 3 // mjtObj.mjOBJ_JOINT
const ACTUATOR_OBJECT = 19 // mjtObj.mjOBJ_ACTUATOR
const FALLEN_TILT_COS = Math.cos((60 * Math.PI) / 180)

export class PicoSim {
    private readonly model: MjModel
    private readonly data: MjData
    private readonly coef: GaitCoef
    private controller: PicoController
    private readonly actuators: number[]
    private readonly jointQpos: number[]
    private readonly substeps: number
    private command: Command = [0, 0, 0]
    private pending = 0

    constructor(
        private readonly mujoco: MainModule,
        assets: SimAssets
    ) {
        writeScene(mujoco, assets)
        this.model = mujoco.MjModel.from_xml_path(`${SCENE_DIR}/scene.xml`)
        this.data = new mujoco.MjData(this.model)
        this.coef = assets.gaitCoef
        this.controller = new PicoController(this.coef)
        this.substeps = Math.round(CONTROL_DT / this.model.opt.timestep)
        this.actuators = JOINT_NAMES.map(name => mujoco.mj_name2id(this.model, ACTUATOR_OBJECT, name))
        this.jointQpos = JOINT_NAMES.map(
            name => this.model.jnt_qposadr[mujoco.mj_name2id(this.model, JOINT_OBJECT, name)]
        )
        this.reset()
    }

    /** Back to the stand pose at the origin, like SpotPicoSim.reset_to_stand, with a fresh gait phase. */
    reset() {
        this.mujoco.mj_resetData(this.model, this.data)
        this.controller = new PicoController(this.coef)
        const stand = new PicoController(this.coef).tick([0, 0, 0])
        this.data.qpos.set([0, 0, STAND_Z, 1, 0, 0, 0], 0)
        this.jointQpos.forEach((address, i) => (this.data.qpos[address] = stand[i]))
        this.actuators.forEach((actuator, i) => (this.data.ctrl[actuator] = stand[i]))
        this.mujoco.mj_forward(this.model, this.data)
        this.pending = 0
    }

    setCommand(cmd: Command) {
        this.command = cmd
    }

    /** Advances by whole 10 ms control ticks, carrying the remainder to the next call. */
    step(seconds: number) {
        this.pending += Math.min(seconds, MAX_FRAME_SECONDS)
        while (this.pending >= CONTROL_DT - 1e-12) {
            const targets = this.controller.tick(this.command)
            this.actuators.forEach((actuator, i) => (this.data.ctrl[actuator] = targets[i]))
            for (let i = 0; i < this.substeps; i++) this.mujoco.mj_step(this.model, this.data)
            this.pending -= CONTROL_DT
        }
    }

    time(): number {
        return this.data.time
    }

    basePosition(): Vec3 {
        return [this.data.qpos[0], this.data.qpos[1], this.data.qpos[2]]
    }

    baseQuaternion(): [number, number, number, number] {
        return [this.data.qpos[3], this.data.qpos[4], this.data.qpos[5], this.data.qpos[6]]
    }

    jointAngles(): Record<string, number> {
        return Object.fromEntries(JOINT_NAMES.map((name, i) => [name, this.data.qpos[this.jointQpos[i]]]))
    }

    /** Below half the stand height, or tilted beyond 60 degrees. */
    hasFallen(): boolean {
        const [w, x, y] = this.baseQuaternion()
        const upZ = 1 - 2 * (x * x + y * y)
        return this.data.qpos[2] < STAND_Z / 2 || upZ < FALLEN_TILT_COS || w === 0
    }

    dispose() {
        this.data.delete()
        this.model.delete()
    }
}

function writeScene(mujoco: MainModule, { sceneXml, meshes }: SimAssets) {
    const fs = mujoco.FS
    for (const dir of [SCENE_DIR, `${SCENE_DIR}/meshes`]) {
        if (!fs.analyzePath(dir).exists) fs.mkdir(dir)
    }
    fs.writeFile(`${SCENE_DIR}/scene.xml`, sceneXml)
    for (const [name, bytes] of Object.entries(meshes)) fs.writeFile(`${SCENE_DIR}/meshes/${name}`, bytes)
}
```

`reset()` takes the stand pose from a throwaway controller's first zero-command tick, which is exactly the IK of the default feet, as `SpotPicoSim.stand_pose` is. The `w === 0` term in `hasFallen` catches a degenerate quaternion after a numerical blow-up.

If `mujoco.FS.analyzePath` is missing from the typings, use `try { fs.mkdir(dir) } catch { /* exists */ }` instead, since the module is shared across `PicoSim` instances.

- [ ] **Step 5: Run it to verify it passes**

Run: `cd app && pnpm exec vitest run tests/unit/pico-sim.spec.ts`
Expected: 4 passed, well under 10 s (a spike on 2026-09-29 stepped the Pico scene 1000 physics steps in 35 ms under Node).

- [ ] **Step 6: Commit**

```bash
git add app/package.json app/pnpm-lock.yaml app/src/lib/simulation/pico-sim.ts app/tests/unit/pico-sim.spec.ts
git commit -m "✨ Simulates the Pico in MuJoCo's WASM build with the ported controller"
```

### Task 5: Controls to simulation command

**Files:**
- Create: `app/src/lib/simulation/controls.ts`
- Test: `app/tests/unit/sim-controls.spec.ts`

**Interfaces:**
- Consumes: `Command` from Task 2; `ControllerData` and `ModesEnum` from `$lib/platform_shared/message`.
- Produces: `simulationCommand(input: ControllerData, mode: ModesEnum): Command`

- [ ] **Step 1: Write the failing test**

```ts
import { describe, it, expect } from 'vitest'
import { simulationCommand } from '../../src/lib/simulation/controls'
import { ControllerData, ModesEnum } from '../../src/lib/platform_shared/message'

const input = (left: [number, number], right: [number, number]) =>
    ControllerData.create({ left: { x: left[0], y: left[1] }, right: { x: right[0], y: right[1] } })

describe('simulation command', () => {
    it('walks forward on the left stick pushed up, as the firmware reads ly', () => {
        expect(simulationCommand(input([0, 1], [0, 0]), ModesEnum.WALK)).toEqual([0.06, 0, 0])
    })

    it('steps sideways against lx and turns with rx, matching walk_state.h', () => {
        const [, vy, yaw] = simulationCommand(input([1, 0], [1, 0]), ModesEnum.WALK)
        expect(vy).toBeCloseTo(-0.03)
        expect(yaw).toBeCloseTo(-2)
    })

    it('clamps a stick beyond its range to the limits', () => {
        expect(simulationCommand(input([0, 1.6], [-3, 0]), ModesEnum.WALK)).toEqual([0.06, 0, 2])
    })

    it('holds the stand pose in every mode but walk', () => {
        for (const mode of [ModesEnum.DEACTIVATED, ModesEnum.IDLE, ModesEnum.REST, ModesEnum.STAND]) {
            expect(simulationCommand(input([0, 1], [1, 0]), mode)).toEqual([0, 0, 0])
        }
    })
})
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cd app && pnpm exec vitest run tests/unit/sim-controls.spec.ts`
Expected: FAIL, `Failed to resolve import "../../src/lib/simulation/controls"`.

- [ ] **Step 3: Write the mapping**

```ts
import { ModesEnum, type ControllerData } from '$lib/platform_shared/message'
import type { Command } from './pico-gait'

// Limits of simulation/src/leika/robot.py.
const MAX_VX = 0.06
const MAX_VY = 0.03
const MAX_YAW = 2.0
const unit = (value: number | undefined) => Math.min(Math.max(value ?? 0, -1), 1)

/**
 * The app's controls as a simulation command, with the firmware's signs (walk_state.h: step_x
 * from ly, step_z from -lx, step_angle from rx; the gait maps a positive step_angle to -yaw).
 */
export function simulationCommand(input: ControllerData, mode: ModesEnum): Command {
    if (mode !== ModesEnum.WALK) return [0, 0, 0]
    // + 0 turns -0 into 0, so a centred stick reads as exactly zero.
    return [
        unit(input.left?.y) * MAX_VX,
        -unit(input.left?.x) * MAX_VY + 0,
        -unit(input.right?.x) * MAX_YAW + 0
    ]
}
```

- [ ] **Step 4: Run it to verify it passes**

Run: `cd app && pnpm exec vitest run tests/unit/sim-controls.spec.ts`
Expected: 4 passed.

- [ ] **Step 5: Commit**

```bash
git add app/src/lib/simulation/controls.ts app/tests/unit/sim-controls.spec.ts
git commit -m "✨ Maps the app's joystick and mode to the simulated Pico's command"
```

### Task 6: The Simulation view

**Files:**
- Create: `app/src/lib/simulation/load.ts`
- Create: `app/src/lib/components/SimulationView.svelte`
- Create: `app/src/lib/components/LazySimulationView.svelte`
- Modify: `app/src/lib/stores/widget-components.ts`, `app/src/lib/stores/application.ts`

**Interfaces:**
- Consumes: `PicoSim`, `SimAssets`, `MAX_FRAME_SECONDS` (Task 4); `simulationCommand` (Task 5); `input`, `mode` stores; `loadModel` from `$lib/utilities/model-utilities` (loads a URDF with a yaw) and the Pico variant's `model`, `stl` and `modelYaw` from `variants.SPOTMICRO_ESP32_MINI`; `SceneBuilder` from `$lib/sceneBuilder`.
- Produces: `loadSimulation(): Promise<{ mujoco: MainModule; assets: SimAssets }>`; widget name `'Simulation'`; a default view named `'Simulation'`.

- [ ] **Step 1: Read the scene builder**

Read `app/src/lib/sceneBuilder.ts` and `app/src/lib/components/Visualization.svelte` (`createScene`, `render`) to reuse the same renderer, ground, lights and orbit controls, so the Simulation view looks like the 3D view.
Note the exact builder calls `Visualization.svelte` makes, and use the same ones in Step 3.

- [ ] **Step 2: Write the loader**

```ts
import loadMujoco, { type MainModule } from '@mujoco/mujoco'
import wasmUrl from '@mujoco/mujoco/mujoco.wasm?url'
import uzip from 'uzip'
import { resolve } from '$app/paths'
import type { SimAssets } from './pico-sim'

let loading: Promise<MainModule> | undefined

/** The WASM is fetched once per page and shared by every Simulation view. */
const mujocoModule = () => (loading ??= loadMujoco({ locateFile: () => wasmUrl }))

const fetchOk = async (path: string) => {
    const response = await fetch(`${resolve('/')}${path}`)
    if (!response.ok) throw new Error(`Could not load ${path}: HTTP ${response.status}`)
    return response
}

export async function loadSimulation(): Promise<{ mujoco: MainModule; assets: SimAssets }> {
    const [mujoco, scene, zip, gait] = await Promise.all([
        mujocoModule(),
        fetchOk('spot_pico_scene.xml').then(r => r.text()),
        fetchOk('spot_pico.zip').then(r => r.arrayBuffer()),
        fetchOk('spot_pico_gait.json').then(r => r.json())
    ])
    const meshes = Object.fromEntries(
        Object.entries(uzip.parse(zip))
            .filter(([name]) => name.endsWith('.stl'))
            .map(([name, bytes]) => [name.slice(name.lastIndexOf('/') + 1), bytes])
    )
    return { mujoco, assets: { sceneXml: scene, meshes, gaitCoef: gait } }
}
```

If the first attempt fails, clear `loading` so Retry fetches again: wrap as `loading = loadMujoco(...).catch(error => { loading = undefined; throw error })`.

- [ ] **Step 3: Write the view**

`SimulationView.svelte` owns one `PicoSim` for its lifetime:
- `onMount`: `loadSimulation()`, then `new PicoSim(mujoco, assets)`, then load the Pico URDF with `loadModel(variants.SPOTMICRO_ESP32_MINI.model, variants.SPOTMICRO_ESP32_MINI.modelYaw)` after `cacheModelFiles` for its `stl` zip, add it to a `SceneBuilder` scene set up exactly as Step 1 noted, and start a `requestAnimationFrame` loop.
- Each frame: `sim.setCommand(simulationCommand(get(input), get(mode).mode))`, `sim.step(elapsedSeconds)`, then copy `sim.jointAngles()` onto `robot.joints[name].setJointValue(angle)` and the base pose onto the model. The base pose maps MuJoCo's Z-up world into the scene's Y-up world with the same rotation `setupRobot` applies: position `(x, y, z)` in MuJoCo becomes the model's position after that rotation, scaled by 10 like the model; the base quaternion composes after the model's fixed rotation.
- `onDestroy`: `cancelAnimationFrame`, `sim.dispose()`.
- States: loading (spinner with "Loading the physics engine (10 MB)"), error (`LoadError` with Retry calling the mount logic again), running, and fallen (a note "The Pico fell over" with a Reset button calling `sim.reset()`).
- A caption "Simulated Pico" at the top, and when `$currentVariant` is not the Pico's, "The simulation always runs the Pico".

`LazySimulationView.svelte`, the same shape as `LazyVisualization.svelte`:

```svelte
<script lang="ts">
    import { HAS_3D_VIEW } from '$lib/build-flags'
</script>

{#if HAS_3D_VIEW}
    {#await import('./SimulationView.svelte') then { default: SimulationView }}
        <SimulationView />
    {/await}
{:else}
    <div class="flex h-full w-full items-center justify-center p-4 text-center text-sm opacity-60">
        The simulation is part of the web app, not the controller built into the robot.
    </div>
{/if}
```

- [ ] **Step 4: Register the widget and the view**

In `widget-components.ts` add `import Simulation from '$lib/components/LazySimulationView.svelte'` and `Simulation` to `WidgetComponents`.
In `application.ts` append to `defaultViews`:

```ts
    {
        name: 'Simulation',
        content: {
            id: 'root',
            layout: 'column',
            widgets: [{ id: 3, component: 'Simulation' }]
        }
    }
```

Views are persisted in `localStorage` (`views`), so existing users will not see a new default; add the Simulation view to a stored list that lacks it, in the same file:

```ts
views.update(list =>
    list.some(view => view.name === 'Simulation') ? list : [...list, defaultViews.at(-1)!]
)
```

- [ ] **Step 5: Check it in a browser**

Run `cd app && pnpm dev`, open `http://localhost:5173/controller`, choose "Simulation" in the view selector, and check with a Playwright script like the earlier controller screenshots:
- the Pico appears and stays standing;
- setting the Walk mode and holding `w` (keyboard left.y) moves it forward;
- leaving the page and returning shows one Pico, and the console shows no errors.

- [ ] **Step 6: Measure the bundle**

Run `cd app && pnpm exec vite build` and `PUBLIC_EMBEDDED_BUILD=true pnpm exec vite build`, reading the sizes Vite prints.
Expected: the embedded bundle stays about 186 KB gzipped (no MuJoCo); the hosted build lists `mujoco-*.wasm` as a separate asset of about 10 MB.
If the hosted `bundle.js` grew by more than 100 KB gzipped from 377 KB, the 293 KB loader was inlined; record the number in the commit and the final summary rather than restructuring the build in this task.

- [ ] **Step 7: Commit**

```bash
git add app/src/lib/simulation/load.ts app/src/lib/components/SimulationView.svelte app/src/lib/components/LazySimulationView.svelte app/src/lib/stores/widget-components.ts app/src/lib/stores/application.ts
git commit -m "✨ Adds a Simulation view where a MuJoCo Pico follows the app's controls"
```

### Task 7: Whole-branch gate

- [ ] **Step 1: Run everything**

Run in `app`: `pnpm exec vitest run`, `pnpm check`, `pnpm exec eslint .`, `pnpm exec prettier --check .`.
Expected: all green; fix what is not.

- [ ] **Step 2: Build the firmware**

Run from the repository root: `pio run -e esp32-camera`.
Expected: SUCCESS; the embedded app contains no `spot_pico_*` files.
