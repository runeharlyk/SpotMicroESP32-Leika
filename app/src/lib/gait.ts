import { get } from 'svelte/store'
import type { body_state_t } from './kinematic'
import { currentKinematic } from './stores/featureFlags'

export interface gait_state_t {
    step_height: number
    step_x: number
    step_z: number
    step_angle: number
    step_velocity: number
    step_depth: number
}

export interface ControllerCommand {
    lx: number
    ly: number
    rx: number
    ry: number
    h: number
    s: number
    s1: number
}

export abstract class GaitState {
    protected abstract name: string

    protected dt = 0.02
    protected body_state!: body_state_t
    protected gait_state: gait_state_t = {
        step_height: 0.4,
        step_x: 0,
        step_z: 0,
        step_angle: 0,
        step_velocity: 1,
        step_depth: 0.002
    }

    public get default_feet_pos() {
        return get(currentKinematic).getDefaultFeetPos()
    }

    protected get default_height() {
        return 0.5
    }

    begin() {
        console.log('Starting', this.name)
    }
    end() {
        console.log('Ending', this.name)
    }
    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        this.map_command(command)
        this.body_state = body_state
        this.dt = dt / 1000

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

    map_command(command: ControllerCommand) {
        const newCommand = {
            step_height: 0.4 + (command.s1 + 1) / 2,
            step_x: command.ly,
            step_z: -command.lx,
            step_velocity: command.s,
            step_angle: command.rx,
            step_depth: 0.002
        }

        this.gait_state = newCommand
    }
}

export class IdleState extends GaitState {
    protected name = 'Idle'

    step(body_state: body_state_t, command: ControllerCommand) {
        super.step(body_state, command)
        return body_state
    }
}

export class CalibrationState extends GaitState {
    protected name = 'Calibration'

    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    step(body_state: body_state_t, _command: ControllerCommand) {
        super.step(body_state, _command)
        body_state.omega = 0
        body_state.phi = 0
        body_state.psi = 0
        body_state.xm = 0
        body_state.ym = this.default_height * 10
        body_state.zm = 0
        body_state.feet = this.default_feet_pos
        return body_state
    }
}

export class RestState extends GaitState {
    protected name = 'Rest'

    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    step(body_state: body_state_t, _command: ControllerCommand) {
        super.step(body_state, _command)
        body_state.omega = 0
        body_state.phi = 0
        body_state.psi = 0
        body_state.xm = 0
        body_state.ym = this.default_height / 2
        body_state.zm = 0
        body_state.feet = this.default_feet_pos
        return body_state
    }
}

export class StandState extends GaitState {
    protected name = 'Stand'

    step(body_state: body_state_t, command: ControllerCommand) {
        super.step(body_state, command)
        body_state.omega = 0
        body_state.phi = command.rx * 10 * (Math.PI / 2)
        body_state.psi = command.ry * 10 * (Math.PI / 2)
        body_state.xm = command.ly / 4
        body_state.zm = command.lx / 4
        body_state.feet = this.default_feet_pos
        return body_state
    }
}

export class BezierState extends GaitState {
    protected name = 'Bezier'
    protected phase = 0
    protected phase_num = 0
    protected step_length = 0
    protected stand_offset = 0.75
    protected mode: 'crawl' | 'trot' = 'trot'
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

    set_mode(mode: 'crawl' | 'trot', duty?: number, order?: [number, number, number, number]) {
        console.log('BezierState set_mode', mode)

        this.mode = mode
        if (mode === 'crawl') {
            this.speed_factor = 0.5
            this.stand_offset = duty ?? 0.85
            const o = order ?? [3, 0, 2, 1]
            const base = [0, 0.25, 0.5, 0.75]
            const offsets = new Array(4).fill(0)
            for (let i = 0; i < 4; i++) offsets[o[i]] = base[i]
            this.offset = offsets
        } else {
            this.speed_factor = 2
            this.stand_offset = duty ?? 0.6
            this.offset = order ? (order.map(v => v % 1) as number[]) : [0, 0.5, 0.5, 0]
        }
    }

    end() {
        super.end()
    }

    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        super.step(body_state, command, dt)
        this.step_length = Math.sqrt(this.gait_state.step_x ** 2 + this.gait_state.step_z ** 2)
        if (this.gait_state.step_x < 0) this.step_length = -this.step_length
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
        this.phase += this.dt * m.step_velocity * this.speed_factor
        if (this.phase >= 1) {
            this.phase_num = (this.phase_num + 1) % 2
            this.phase = 0
        }
    }

    update_body_position() {
        const m = this.gait_state
        const moving = m.step_x !== 0 || m.step_z !== 0 || m.step_angle !== 0
        if (!moving) return

        if (this.mode !== 'crawl') return

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
        let length = this.step_length / 2
        let angle = Math.atan2(this.gait_state.step_z, this.step_length) * 2
        const delta_pos = controller(length, angle, ...args, phase)

        length = this.gait_state.step_angle * 2
        angle = yawArc(this.default_feet_pos[index], this.body_state.feet[index])

        const delta_rot = controller(length, angle, ...args, phase)

        this.body_state.feet[index][0] += delta_pos[0] + delta_rot[0] * 0.2
        this.body_state.feet[index][2] += delta_pos[2] + delta_rot[2] * 0.2
        if (this.gait_state.step_x || this.gait_state.step_z || this.gait_state.step_angle)
            this.body_state.feet[index][1] += delta_pos[1] + delta_rot[1] * 0.2

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

const yawArc = (default_foot_pos: number[], current_foot_pos: number[]): number => {
    const foot_mag = Math.sqrt(default_foot_pos[0] ** 2 + default_foot_pos[2] ** 2)
    const foot_dir = Math.atan2(default_foot_pos[2], default_foot_pos[0])
    const offsets = [
        current_foot_pos[0] - default_foot_pos[0],
        current_foot_pos[2] - default_foot_pos[2],
        current_foot_pos[1] - default_foot_pos[1]
    ]
    const offset_mag = Math.sqrt(offsets[0] ** 2 + offsets[2] ** 2)
    const offset_mod = Math.atan2(offset_mag, foot_mag)

    return Math.PI / 2.0 + foot_dir + offset_mod
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
};

/*
Units:  meters, radians, seconds / beats
*/

// interface Options {
//     controls: 'body' | 'legs' | 'both';

//     extendable?: boolean; // if true, the animation can loop
//     description?: string; // a description of the animation
// }

interface Frame {
    time: number;
    position: number[];
    orientation: number[];
    feet?: number[][];
}

type Parameter = {
    // name: string;
    min: number;
    max: number;
    default: number;
};

type Parameters = Record<string, Parameter>;

interface Animation {
    // options: Options = {};
    parameters: Parameters;
    frames: Frame[];
}

const generateCircleAnimation = (
    radius: number,
    y: number,
    duration: number,
    segments: number
): Animation => {
    const frames: Frame[] = [];
    const deltaTime = duration / segments;

    for (let i = 0; i <= segments; i++) {
        const angle = (2 * Math.PI * i) / segments; // Angle in radians
        const x = radius * Math.cos(angle);
        const z = radius * Math.sin(angle);

        frames.push({
            time: i * deltaTime,
            position: [x, y, z],
            orientation: [0, 0, 0]
        });
    }

    return {
        parameters: {
            speed: { min: 0.1, max: 2, default: 1 },
            x_offset: { min: -0.1, max: 0.1, default: 0 }
        },
        frames
    };
};

const kinematicShowCaseGen = generateCircleAnimation(0.5, 0.7, 4000, 32);

const kinematicShowCase: Animation = {
    parameters: {
        speed: { min: 0.1, max: 2, default: 1 },
        x_offset: { min: -0.1, max: 0.1, default: 0 }
    },
    frames: [
        {
            time: 0,
            position: [0.5, 0.7, 0],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 500,
            position: [0.3, 0.7, 0.3],
            orientation: [0, 0, 0]
        },
        {
            time: 1000,
            position: [0, 0.7, 0.5],
            orientation: [0, 0, 0]
        },
        {
            time: 1500,
            position: [-0.3, 0.7, 0.3],
            orientation: [0, 0, 0]
        },
        {
            time: 2000,
            position: [-0.5, 0.7, 0],
            orientation: [0, 0, 0]
        },
        {
            time: 2500,
            position: [-0.3, 0.7, -0.3],
            orientation: [0, 0, 0]
        },
        {
            time: 3000,
            position: [0, 0.7, -0.5],
            orientation: [0, 0, 0]
        },
        {
            time: 3500,
            position: [0.3, 0.7, -0.3],
            orientation: [0, 0, 0]
        },
        {
            time: 4000,
            position: [0.5, 0.7, 0],
            orientation: [0, 0, 0]
        }
    ]
};

const stretch: Animation = {
    parameters: {
        speed: { min: 0.1, max: 2, default: 1 },
        x_offset: { min: -0.1, max: 0.1, default: 0 }
    },
    frames: [
        // Step forward
        {
            time: 0,
            position: [0, 0.7, 0],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 250,
            position: [0, 0.7, -0.2],
            orientation: [0, 0, 0],
            feet: [
                [1.5, -0.5, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 500,
            position: [0, 0.7, -0.2],
            orientation: [0, 0, 0],
            feet: [
                [2, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 750,
            position: [0, 0.7, 0.2],
            orientation: [0, 0, 0],
            feet: [
                [2, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 1000,
            position: [0, 0.7, 0.2],
            orientation: [0, 0, 0],
            feet: [
                [2, -1, 1, 1],
                [1.5, -0.5, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 1250,
            position: [0, 0.7, 0.2],
            orientation: [0, 0, 0],
            feet: [
                [2, -1, 1, 1],
                [2, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 2500,
            position: [0.5, 0.7, 0],
            orientation: [0, 0, 25]
        },
        {
            time: 4000,
            position: [-0.7, 0.7, 0],
            orientation: [0, 0, -20]
        },
        {
            time: 5000,
            position: [-0.7, 0.7, 0],
            orientation: [0, 0, -20]
        },
        {
            time: 6000,
            position: [0, 0.7, 0],
            orientation: [0, 0, 0]
        },
        {
            time: 6000,
            position: [-0.2, 0.7, -0.2],
            orientation: [0, 0, 0],
            feet: [
                [2, -1, 1, 1],
                [2, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 6500,
            position: [-0.2, 0.7, -0.2],
            orientation: [0, 0, 0],
            feet: [
                [0.5, -0.5, 1, 1],
                [2, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 7000,
            position: [-0.2, 0.7, 0.2],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [2, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 7500,
            position: [-0.2, 0.7, 0.2],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [0.5, -0.5, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 8000,
            position: [0, 0.7, 0],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        }
    ]
};

const pee: Animation = {
    parameters: {
        speed: { min: 0.1, max: 2, default: 1 },
        x_offset: { min: -0.1, max: 0.1, default: 0 }
    },
    frames: [
        {
            time: 0,
            position: [0, 0.7, 0],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 1000,
            position: [0, 0.7, 0],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 2000,
            position: [0.2, 0.7, 0.45],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 3000,
            position: [0.2, 0.7, 0.45],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 4000,
            position: [0.2, 0.7, 0.45],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, 0, -1, 1]
            ]
        },
        {
            time: 5000,
            position: [0.2, 0.7, 0.45],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        },
        {
            time: 6000,
            position: [0, 0.7, 0],
            orientation: [0, 0, 0],
            feet: [
                [1, -1, 1, 1],
                [1, -1, -1, 1],
                [-1, -1, 1, 1],
                [-1, -1, -1, 1]
            ]
        }
    ]
};

export class Animater extends GaitState {
    protected name = 'Bezier';
    time = 0;
    animation = pee; // stretch;

    begin() {
        this.time = 0;
        super.begin();
    }

    end() {
        this.time = 0;
        super.end();
    }

    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        return this.step_animation(body_state, dt);
    }

    step_animation(body_state: body_state_t, dt: number = 0.02) {
        this.dt = dt / 1000;
        this.time += dt;

        const duration = this.animation.frames[this.animation.frames.length - 1].time;
        if (this.time > duration) {
            this.time = this.time % duration;
        }

        const { prevFrame, nextFrame } = this.getBoundingFrames();

        const t = this.getInterpolationFactor(prevFrame, nextFrame);
        const position = this.interpolatePosition(prevFrame.position, nextFrame.position, t);
        const orientation = this.interpolatePosition(
            prevFrame.orientation,
            nextFrame.orientation,
            t
        );

        // Apply x_offset
        // position[0] += this.xOffset;

        body_state.xm = position[0];
        body_state.ym = position[1];
        body_state.zm = position[2];
        body_state.omega = orientation[0];
        body_state.phi = orientation[1];
        body_state.psi = orientation[2];

        if (prevFrame.feet && nextFrame.feet) {
            for (let i = 0; i < 4; i++) {
                body_state.feet[i] = this.interpolatePosition(
                    prevFrame.feet[i],
                    nextFrame.feet[i],
                    t
                );
            }
        }

        return body_state;
    }

    private getBoundingFrames(): { prevFrame: Frame; nextFrame: Frame } {
        const frames = this.animation.frames;

        for (let i = 0; i < frames.length - 1; i++) {
            const prevFrame = frames[i];
            const nextFrame = frames[i + 1];

            if (this.time >= prevFrame.time && this.time <= nextFrame.time) {
                return { prevFrame, nextFrame };
            }
        }

        // Fallback (should not be reached if looping correctly)
        return { prevFrame: frames[frames.length - 1], nextFrame: frames[0] };
    }

    private getInterpolationFactor(prevFrame: Frame, nextFrame: Frame): number {
        const timeRange = nextFrame.time - prevFrame.time;
        const elapsed = this.time - prevFrame.time;
        return elapsed / timeRange;
    }

    private interpolatePosition(pos1: number[], pos2: number[], t: number): number[] {
        return pos1.map((val, index) => val + t * (pos2[index] - val));
    }
}
