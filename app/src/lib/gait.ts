import type { body_state_t } from './kinematic';
import { fromInt8 } from './utilities';

const { sin } = Math;

export interface gait_state_t {
    step_height: number;
    step_x: number;
    step_z: number;
    step_angle: number;
    step_velocity: number;
    step_depth: number;
}

export interface ControllerCommand {
    stop: number;
    lx: number;
    ly: number;
    rx: number;
    ry: number;
    h: number;
    s: number;
}

export abstract class GaitState {
    protected abstract name: string;

    protected dt = 0.02;
    protected body_state!: body_state_t;
    protected gait_state: gait_state_t = {
        step_height: 0.4,
        step_x: 0,
        step_z: 0,
        step_angle: 0,
        step_velocity: 1,
        step_depth: 0.002
    };

    public get default_feet_pos() {
        return [
            [1, -1, 1, 1],
            [1, -1, -1, 1],
            [-1, -1, 1, 1],
            [-1, -1, -1, 1]
        ];
    }

    protected get default_height() {
        return 0.5;
    }

    begin() {
        console.log('Starting', this.name);
    }
    end() {
        console.log('Ending', this.name);
    }
    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        this.map_command(command);
        this.body_state = body_state;
        this.dt = dt / 1000;
        return body_state;
    }

    map_command(command: ControllerCommand) {
        const newCommand = {
            step_height: 0.4,
            step_x: Math.floor(fromInt8(command.ly, -1, 1) * 10) / 10,
            step_z: -(Math.floor(fromInt8(command.lx, -1, 1) * 10) / 10),
            step_velocity: command.s / 128 + 1,
            step_angle: command.rx / 128,
            step_depth: 0.002
        };

        this.gait_state = newCommand;
    }
}

export class IdleState extends GaitState {
    protected name = 'Idle';
}

export class CalibrationState extends GaitState {
    protected name = 'Calibration';

    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        body_state.omega = 0;
        body_state.phi = 0;
        body_state.psi = 0;
        body_state.xm = 0;
        body_state.ym = this.default_height * 10;
        body_state.zm = 0;
        body_state.feet = this.default_feet_pos;
        return body_state;
    }
}

export class RestState extends GaitState {
    protected name = 'Rest';

    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        body_state.omega = 0;
        body_state.phi = 0;
        body_state.psi = 0;
        body_state.xm = 0;
        body_state.ym = this.default_height / 2;
        body_state.zm = 0;
        body_state.feet = this.default_feet_pos;
        return body_state;
    }
}

export class StandState extends GaitState {
    protected name = 'Stand';

    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        body_state.omega = 0;
        body_state.phi = command.rx / 8;
        body_state.psi = command.ry / 8;
        body_state.xm = command.ly / 2 / 100;
        body_state.zm = command.lx / 2 / 100;
        body_state.feet = this.default_feet_pos;
        return body_state;
    }
}

abstract class PhaseGaitState extends GaitState {
    protected tick = 0;
    protected phase = 0;
    protected phase_time = 0;
    protected abstract num_phases: number;
    protected abstract phase_speed_factor: number;
    protected abstract swing_stand_ratio: number;

    protected contact_phases!: number[][];
    protected shifts!: number[][];

    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        super.step(body_state, command, dt);
        this.update_phase();
        this.update_body_position();
        this.update_feet_positions();
        return this.body_state;
    }

    update_phase() {
        this.phase_time += this.dt * this.phase_speed_factor * this.gait_state.step_velocity;

        if (this.phase_time >= 1) {
            this.phase += 1;
            if (this.phase == this.num_phases) this.phase = 0;
            this.phase_time = 0;
        }
    }

    update_body_position() {
        if (this.num_phases === 4) return;

        const shift = this.shifts[Math.floor(this.phase / 2)];

        this.body_state.xm += (shift[0] - this.body_state.xm) * this.dt * 4;
        this.body_state.zm += (shift[2] - this.body_state.zm) * this.dt * 4;
    }

    update_feet_positions() {
        for (let i = 0; i < 4; i++) {
            this.body_state.feet[i] = this.update_foot_position(i);
        }
    }

    update_foot_position(index: number): number[] {
        const contact = this.contact_phases[index][this.phase];
        return contact ? this.stand(index) : this.swing(index);
    }

    stand(index: number): number[] {
        const delta_pos = [
            -this.gait_state.step_x * this.dt * this.swing_stand_ratio,
            0,
            -this.gait_state.step_z * this.dt * this.swing_stand_ratio
        ];

        this.body_state.feet[index][0] = this.body_state.feet[index][0] + delta_pos[0];
        this.body_state.feet[index][1] = this.default_feet_pos[index][1];
        this.body_state.feet[index][2] = this.body_state.feet[index][2] + delta_pos[2];
        return this.body_state.feet[index];
    }

    swing(index: number): number[] {
        const delta_pos = [this.gait_state.step_x * this.dt, 0, this.gait_state.step_z * this.dt];

        if (this.gait_state.step_x == 0) {
            delta_pos[0] =
                (this.default_feet_pos[index][0] - this.body_state.feet[index][0]) * this.dt * 8;
        }

        if (this.gait_state.step_z == 0) {
            delta_pos[2] =
                (this.default_feet_pos[index][2] - this.body_state.feet[index][2]) * this.dt * 8;
        }

        this.body_state.feet[index][0] = this.body_state.feet[index][0] + delta_pos[0];
        this.body_state.feet[index][1] =
            this.default_feet_pos[index][1] +
            sin(this.phase_time * Math.PI) * this.gait_state.step_height;
        this.body_state.feet[index][2] = this.body_state.feet[index][2] + delta_pos[2];
        return this.body_state.feet[index];
    }
}

export class FourPhaseWalkState extends PhaseGaitState {
    protected name = 'Four phase walk';
    protected num_phases = 4;
    protected phase_speed_factor = 6;
    protected contact_phases = [
        [1, 0, 1, 1],
        [1, 1, 1, 0],
        [1, 1, 1, 0],
        [1, 0, 1, 1]
    ];
    protected swing_stand_ratio = 1 / (this.num_phases - 1);

    begin() {
        super.begin();
    }

    end() {
        super.end();
    }

    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        return super.step(body_state, command, dt);
    }
}

export class EightPhaseWalkState extends PhaseGaitState {
    protected name = 'Eight phase walk';
    protected num_phases = 8;
    protected phase_speed_factor = 4;
    protected contact_phases = [
        [1, 0, 1, 1, 1, 1, 1, 1],
        [1, 1, 1, 1, 1, 0, 1, 1],
        [1, 1, 1, 1, 1, 1, 1, 0],
        [1, 1, 1, 0, 1, 1, 1, 1]
    ];
    protected shifts = [
        [-0.05, 0, -0.2],
        [0.3, 0, 0.2],
        [-0.05, 0, 0.2],
        [0.3, 0, -0.2]
    ];
    protected swing_stand_ratio = 1 / (this.num_phases - 1);

    begin() {
        super.begin();
    }

    end() {
        super.end();
    }

    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        return super.step(body_state, command, dt);
    }
}

export class BezierState extends GaitState {
    protected name = 'Bezier';
    protected phase = 0;
    protected phase_num = 0;
    protected contact_phases = [
        [1, 0],
        [0, 1],
        [0, 1],
        [1, 0]
    ];
    protected step_length: number = 0;

    begin() {
        super.begin();
    }

    end() {
        super.end();
    }

    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        super.step(body_state, command, dt);
        this.step_length = Math.sqrt(this.gait_state.step_x ** 2 + this.gait_state.step_z ** 2);
        if (this.gait_state.step_x < 0) {
            this.step_length = -this.step_length;
            this.gait_state.step_z = -this.gait_state.step_z;
        }
        this.update_phase();
        this.update_feet_positions();
        return this.body_state;
    }

    update_phase() {
        this.phase += this.dt * this.gait_state.step_velocity * 2;
        if (this.phase >= 1) {
            this.phase_num += 1;
            this.phase_num %= 2;
            this.phase = 0;
        }
    }

    update_feet_positions() {
        for (let i = 0; i < 4; i++) {
            this.body_state.feet[i] = this.update_foot_position(i);
        }
    }

    update_foot_position(index: number): number[] {
        const contact = this.contact_phases[index][this.phase_num];
        this.body_state.feet[index][0] = this.default_feet_pos[index][0];
        this.body_state.feet[index][1] = this.default_feet_pos[index][1];
        this.body_state.feet[index][2] = this.default_feet_pos[index][2];
        return contact ? this.swing_controller(index) : this.stand_controller(index);
    }

    stand_controller(index: number) {
        let depth = this.gait_state.step_depth;
        return this.controller(index, stance_curve, depth);
    }

    swing_controller(index: number) {
        let height = this.gait_state.step_height;
        return this.controller(index, bezier_curve, height);
    }

    controller(
        index: number,
        controller: (length: number, angle: number, ...args: number[]) => number[],
        ...args: number[]
    ) {
        let length = this.step_length / 2;
        let angle = (this.gait_state.step_z * Math.PI) / 2;
        const delta_pos = controller(length, angle, ...args, this.phase);

        length = this.gait_state.step_angle * 2;
        angle = yawArc(this.default_feet_pos[index], this.body_state.feet[index]);

        const delta_rot = controller(length, angle, ...args, this.phase);

        this.body_state.feet[index][0] += delta_pos[0] + delta_rot[0] * 0.2;
        this.body_state.feet[index][2] += delta_pos[2] + delta_rot[2] * 0.2;
        if (this.gait_state.step_x || this.gait_state.step_z || this.gait_state.step_angle)
            this.body_state.feet[index][1] += delta_pos[1] + delta_rot[1] * 0.2;

        return this.body_state.feet[index];
    }
}

const stance_curve = (length: number, angle: number, depth: number, phase: number): number[] => {
    const X_POLAR = Math.cos(angle);
    const Y_POLAR = Math.sin(angle);

    const step = length * (1 - 2 * phase);
    const X = step * X_POLAR;
    const Y = step * Y_POLAR;
    let Z = 0;

    if (length !== 0) {
        Z = -depth * Math.cos((Math.PI * (X + Y)) / (2 * length));
    }
    return [X, Z, Y];
};

const yawArc = (default_foot_pos: number[], current_foot_pos: number[]): number => {
    const foot_mag = Math.sqrt(default_foot_pos[0] ** 2 + default_foot_pos[2] ** 2);
    const foot_dir = Math.atan2(default_foot_pos[2], default_foot_pos[0]);
    const offsets = [
        current_foot_pos[0] - default_foot_pos[0],
        current_foot_pos[2] - default_foot_pos[2],
        current_foot_pos[1] - default_foot_pos[1]
    ];
    const offset_mag = Math.sqrt(offsets[0] ** 2 + offsets[2] ** 2);
    const offset_mod = Math.atan2(offset_mag, foot_mag);

    return Math.PI / 2.0 + foot_dir + offset_mod;
};

const bezier_curve = (length: number, angle: number, height: number, phase: number): number[] => {
    const control_points = get_control_points(length, angle, height);
    const n = control_points.length - 1;

    const point = [0, 0, 0];
    for (let i = 0; i <= n; i++) {
        const bernstein_poly = comb(n, i) * Math.pow(phase, i) * Math.pow(1 - phase, n - i);
        point[0] += bernstein_poly * control_points[i][0];
        point[1] += bernstein_poly * control_points[i][1];
        point[2] += bernstein_poly * control_points[i][2];
    }
    return point;
};
const get_control_points = (length: number, angle: number, height: number): number[][] => {
    const X_POLAR = Math.cos(angle);
    const Z_POLAR = Math.sin(angle);

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
    ];

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
    ];

    const control_points: number[][] = [];

    for (let i = 0; i < STEP.length; i++) {
        const X = STEP[i] * X_POLAR;
        const Z = STEP[i] * Z_POLAR;
        control_points.push([X, Y[i], Z]);
    }

    return control_points;
};

const comb = (n: number, k: number): number => {
    if (k < 0 || k > n) return 0;
    if (k === 0 || k === n) return 1;
    k = Math.min(k, n - k);
    let c = 1;
    for (let i = 0; i < k; i++) {
        c = (c * (n - i)) / (i + 1);
    }
    return c;
};
