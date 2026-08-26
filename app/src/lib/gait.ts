import { get } from 'svelte/store'
import type { body_state_t } from './kinematic'
import { currentKinematic } from './stores/featureFlags'
import { ControllerData, WalkGaits } from './platform_shared/message'

export interface gait_state_t {
    step_height: number
    step_x: number
    step_z: number
    step_angle: number
    step_velocity: number
    step_depth: number
}

export abstract class GaitState {
    protected abstract name: string

    protected dt = 0.02
    protected body_state!: body_state_t

    protected get kinematic() {
        return get(currentKinematic)
    }

    protected gait_state: gait_state_t = {
        step_height: 0,
        step_x: 0,
        step_z: 0,
        step_angle: 0,
        step_velocity: 1,
        step_depth: 0
    }

    public get default_feet_pos() {
        return this.kinematic.getDefaultFeetPos()
    }

    protected get default_height() {
        return this.kinematic.default_body_height
    }

    protected get default_step_depth() {
        return this.kinematic.default_step_depth
    }

    protected get default_step_height() {
        return this.kinematic.default_step_height
    }

    begin() {
        console.log('Starting', this.name)
    }
    end() {
        console.log('Ending', this.name)
    }
    step(body_state: body_state_t, command: ControllerData, dt_ms: number = 20) {
        this.map_command(command)
        this.body_state = body_state
        this.dt = dt_ms / 1000

        if (body_state.cumulative_x === undefined) {
            body_state.cumulative_x = 0
            body_state.cumulative_y = 0
            body_state.cumulative_z = 0
            body_state.cumulative_roll = 0
            body_state.cumulative_pitch = 0
            body_state.cumulative_yaw = 0
        }

        return body_state
    }

    map_command(command: ControllerData) {
        const kin = this.kinematic
        this.gait_state = {
            step_height: command.s1 * kin.max_step_height,
            step_x: command.left!.y * kin.max_step_length,
            step_z: -command.left!.x * kin.max_step_length,
            step_velocity: command.speed,
            step_angle: command.right!.x,
            step_depth: kin.default_step_depth
        }
    }
}

export class IdleState extends GaitState {
    protected name = 'Idle'
    step(body_state: body_state_t, command: ControllerData) {
        super.step(body_state, command)
        return body_state
    }
}

export class CalibrationState extends GaitState {
    protected name = 'Calibration'

    step(body_state: body_state_t, _command: ControllerData) {
        super.step(body_state, _command)
        body_state.omega = 0
        body_state.phi = 0
        body_state.psi = 0
        body_state.xm = 0
        body_state.ym = this.kinematic.max_body_height
        body_state.zm = 0
        body_state.feet = this.default_feet_pos
        return body_state
    }
}

export class RestState extends GaitState {
    protected name = 'Rest'

    step(body_state: body_state_t, _command: ControllerData) {
        super.step(body_state, _command)
        body_state.omega = 0
        body_state.phi = 0
        body_state.psi = 0
        body_state.xm = 0
        body_state.ym = this.kinematic.min_body_height
        body_state.zm = 0
        body_state.feet = this.default_feet_pos
        return body_state
    }
}

export class StandState extends GaitState {
    protected name = 'Stand'

    step(body_state: body_state_t, command: ControllerData) {
        super.step(body_state, command)
        const kin = this.kinematic
        body_state.omega = 0
        body_state.ym = kin.min_body_height + command.height * kin.body_height_range
        body_state.psi = command.right!.y * kin.max_pitch
        body_state.phi = command.right!.x * kin.max_roll
        body_state.xm = command.left!.y * kin.max_body_shift_x
        body_state.zm = command.left!.x * kin.max_body_shift_z
        body_state.feet = this.default_feet_pos
        return body_state
    }
}

export class BezierState extends GaitState {
    protected name = 'Bezier'
    protected phase = 0
    protected phase_num = 0
    protected stand_offset = 0.75
    protected duty_slow = 0.85
    protected duty_fast = 0.5
    protected mode: WalkGaits = WalkGaits.TROT
    protected speed_factor = 1
    offset = [0, 0.5, 0.75, 0.25]

    protected shift_start_pos = { x: 0, z: 0 }
    protected shift_target_pos = { x: 0, z: 0 }
    protected shift_start_time = 0
    protected current_shift_leg = -1

    protected last_body_state: body_state_t | null = null
    protected cumulative_position = { x: 0, y: 0, z: 0 }
    protected cumulative_orientation = { roll: 0, pitch: 0, yaw: 0 }

    constructor() {
        super()
        this.set_mode(this.mode)
    }

    begin() {
        super.begin()
    }

    set_mode(mode: WalkGaits, order?: [number, number, number, number]) {
        this.mode = mode
        if (mode === WalkGaits.CRAWL) {
            this.speed_factor = 0.5
            this.duty_slow = 0.9
            this.duty_fast = 0.8
            const o = order ?? [3, 0, 2, 1]
            const base = [0, 0.25, 0.5, 0.75]
            const offsets = new Array(4).fill(0)
            for (let i = 0; i < 4; i++) offsets[o[i]] = base[i]
            this.offset = offsets
        } else {
            this.speed_factor = 2
            this.duty_slow = 0.85
            this.duty_fast = 0.5
            this.offset = order ? (order.map(v => v % 1) as number[]) : [0, 0.5, 0.5, 0]
        }
    }

    end() {
        super.end()
    }

    step(body_state: body_state_t, command: ControllerData, dt_ms: number = 20) {
        super.step(body_state, command, dt_ms)
        const kin = this.kinematic
        this.body_state.ym = kin.min_body_height + command.height * kin.body_height_range
        // Duty factor scales with commanded velocity, mirroring the firmware.
        const v = Math.max(0, Math.min(1, this.gait_state.step_velocity))
        this.stand_offset = this.lerp(
            this.stand_offset,
            this.lerp(this.duty_slow, this.duty_fast, v),
            0.1
        )
        this.update_phase()
        this.update_body_position()
        this.update_feet_positions()
        this.update_cumulative_position()
        return this.body_state
    }

    update_phase() {
        const m = this.gait_state
        if (m.step_x === 0 && m.step_z === 0 && m.step_angle === 0) {
            this.phase = 0
            return
        }
        this.phase += this.dt * Math.max(m.step_velocity, 0.5) * this.speed_factor
        if (this.phase >= 1) {
            this.phase_num = (this.phase_num + 1) % 2
            this.phase = 0
        }
    }

    update_body_position() {
        const m = this.gait_state
        const moving = m.step_x !== 0 || m.step_z !== 0 || m.step_angle !== 0
        if (!moving) return

        if (this.mode !== WalkGaits.CRAWL) return

        const { stance, swing, next_swing, time_to_lift } = this.get_leg_states()

        if (stance.length >= 3 && swing.length === 0 && next_swing !== -1) {
            if (this.current_shift_leg !== next_swing) {
                this.current_shift_leg = next_swing
                this.shift_start_pos.x = this.body_state.xm
                this.shift_start_pos.z = this.body_state.zm

                const remaining_legs = stance.filter(leg => leg !== next_swing)
                const target = this.stance_centroid(remaining_legs)
                this.shift_target_pos.x = target[0]
                this.shift_target_pos.z = target[2]

                this.shift_start_time = time_to_lift
            }

            const total_time = this.shift_start_time
            const progress = total_time > 0 ? 1 - time_to_lift / total_time : 1
            const smooth_progress = this.smoothstep01(Math.max(0, Math.min(1, progress)))

            this.body_state.xm = this.lerp(
                this.shift_start_pos.x,
                this.shift_target_pos.x,
                smooth_progress
            )
            this.body_state.zm = this.lerp(
                this.shift_start_pos.z,
                this.shift_target_pos.z,
                smooth_progress
            )
        }
    }

    protected lerp(a: number, b: number, t: number): number {
        return a + (b - a) * t
    }

    protected stance_centroid(legs: number[]): number[] {
        if (legs.length === 0) return [this.body_state.xm, 0, this.body_state.zm]

        let sx = 0,
            sz = 0
        for (const i of legs) {
            sx += this.body_state.feet[i][0]
            sz += this.body_state.feet[i][2]
        }
        return [sx / legs.length, 0, sz / legs.length]
    }

    protected get_leg_states(): {
        stance: number[]
        swing: number[]
        next_swing: number
        time_to_lift: number
    } {
        const stance: number[] = []
        const swing: number[] = []
        let next_swing = -1
        let min_time_to_swing = Infinity

        for (let i = 0; i < 4; i++) {
            let phase = this.phase + this.offset[i]
            if (phase >= 1) phase -= 1

            if (phase <= this.stand_offset) {
                stance.push(i)
                const time_to_swing = this.stand_offset - phase
                if (time_to_swing < min_time_to_swing) {
                    min_time_to_swing = time_to_swing
                    next_swing = i
                }
            } else {
                swing.push(i)
            }
        }

        return { stance, swing, next_swing, time_to_lift: min_time_to_swing }
    }

    protected smoothstep01(t: number): number {
        const x = Math.max(0, Math.min(1, t))
        return x * x * (3 - 2 * x)
    }

    update_feet_positions() {
        for (let i = 0; i < 4; i++) this.body_state.feet[i] = this.update_foot_position(i)
    }

    update_foot_position(index: number): number[] {
        let phase = this.phase + this.offset[index]
        if (phase >= 1) phase -= 1
        this.body_state.feet[index][0] = this.default_feet_pos[index][0]
        this.body_state.feet[index][1] = this.default_feet_pos[index][1]
        this.body_state.feet[index][2] = this.default_feet_pos[index][2]
        return phase <= this.stand_offset ?
                this.stand_controller(index, phase / this.stand_offset)
            :   this.swing_controller(index, (phase - this.stand_offset) / (1 - this.stand_offset))
    }

    stand_controller(index: number, phase: number) {
        const depth = this.gait_state.step_depth
        return this.controller(index, phase, stance_curve, depth)
    }

    swing_controller(index: number, phase: number) {
        const height = this.gait_state.step_height
        return this.controller(index, phase, bezier_curve, height)
    }

    controller(
        index: number,
        phase: number,
        controller: (length: number, angle: number, ...args: number[]) => number[],
        ...args: number[]
    ) {
        // Each foot's stroke is the rigid-body velocity field at its stance position: the commanded
        // translation plus the rotational contribution omega x r about the body centre. Composing
        // both into one vector means a single curve, so the swing/stance profile is applied once.
        const m = this.gait_state
        const rx = this.default_feet_pos[index][0]
        const rz = this.default_feet_pos[index][2]
        const stroke_x = m.step_x + m.step_angle * -rz
        const stroke_z = m.step_z + m.step_angle * rx
        const stroke = Math.hypot(stroke_x, stroke_z)

        const delta = controller(stroke / 2, Math.atan2(stroke_z, stroke_x), ...args, phase)

        this.body_state.feet[index][0] += delta[0]
        this.body_state.feet[index][2] += delta[2]
        if (stroke !== 0) this.body_state.feet[index][1] += delta[1]

        return this.body_state.feet[index]
    }

    update_cumulative_position() {
        if (this.last_body_state === null) {
            this.last_body_state = { ...this.body_state }
            this.body_state.cumulative_x = 0
            this.body_state.cumulative_y = 0
            this.body_state.cumulative_z = 0
            this.body_state.cumulative_roll = 0
            this.body_state.cumulative_pitch = 0
            this.body_state.cumulative_yaw = 0
            return
        }

        const m = this.gait_state
        const moving = m.step_x !== 0 || m.step_z !== 0 || m.step_angle !== 0

        if (moving) {
            const step_displacement_x_local =
                m.step_x * m.step_velocity * this.dt * this.speed_factor
            const step_displacement_z_local =
                m.step_z * m.step_velocity * this.dt * this.speed_factor
            const step_displacement_yaw =
                m.step_angle * m.step_velocity * this.dt * this.speed_factor

            const cos_yaw = Math.cos(this.cumulative_orientation.yaw)
            const sin_yaw = Math.sin(this.cumulative_orientation.yaw)
            const step_displacement_x =
                step_displacement_x_local * cos_yaw - step_displacement_z_local * sin_yaw
            const step_displacement_z =
                step_displacement_x_local * sin_yaw + step_displacement_z_local * cos_yaw

            this.cumulative_position.x += step_displacement_x
            this.cumulative_position.z += step_displacement_z
            this.cumulative_orientation.yaw += step_displacement_yaw
        }

        this.body_state.cumulative_x = this.cumulative_position.x
        this.body_state.cumulative_y = this.cumulative_position.y
        this.body_state.cumulative_z = this.cumulative_position.z
        this.body_state.cumulative_roll = this.cumulative_orientation.roll
        this.body_state.cumulative_pitch = this.cumulative_orientation.pitch
        this.body_state.cumulative_yaw = this.cumulative_orientation.yaw

        this.last_body_state = { ...this.body_state }
    }
}

const stance_curve = (length: number, angle: number, depth: number, phase: number): number[] => {
    const X_POLAR = Math.cos(angle)
    const Y_POLAR = Math.sin(angle)

    const step = length * (1 - 2 * phase)
    const X = step * X_POLAR
    const Z = step * Y_POLAR
    let Y = 0
    if (length !== 0) Y = -depth * Math.cos((Math.PI * (X + Y)) / (2 * length))
    return [X, Y, Z]
}

const bezier_curve = (length: number, angle: number, height: number, phase: number): number[] => {
    const control_points = get_control_points(length, angle, height)
    const n = control_points.length - 1

    const point = [0, 0, 0]
    for (let i = 0; i <= n; i++) {
        const bernstein_poly = comb(n, i) * Math.pow(phase, i) * Math.pow(1 - phase, n - i)
        point[0] += bernstein_poly * control_points[i][0]
        point[1] += bernstein_poly * control_points[i][1]
        point[2] += bernstein_poly * control_points[i][2]
    }
    return point
}

const get_control_points = (length: number, angle: number, height: number): number[][] => {
    const X_POLAR = Math.cos(angle)
    const Z_POLAR = Math.sin(angle)

    const STEP = [
        -length,
        -length * 1.4,
        -length * 1.5,
        -length * 1.5,
        -length * 1.5,
        0.0,
        0.0,
        0.0,
        length * 1.5,
        length * 1.5,
        length * 1.4,
        length
    ]

    const Y = [
        0.0,
        0.0,
        height * 0.9,
        height * 0.9,
        height * 0.9,
        height * 0.9,
        height * 0.9,
        height * 1.1,
        height * 1.1,
        height * 1.1,
        0.0,
        0.0
    ]

    const control_points: number[][] = []

    for (let i = 0; i < STEP.length; i++) {
        const X = STEP[i] * X_POLAR
        const Z = STEP[i] * Z_POLAR
        control_points.push([X, Y[i], Z])
    }

    return control_points
}

const comb = (n: number, k: number): number => {
    if (k < 0 || k > n) return 0
    if (k === 0 || k === n) return 1
    k = Math.min(k, n - k)
    let c = 1
    for (let i = 0; i < k; i++) c = (c * (n - i)) / (i + 1)
    return c
}
