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

    protected static body_state: body_state_t;

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
        return body_state;
    }

    map_command(command: ControllerCommand): gait_state_t {
        return {
            step_height: 0.4 + Math.abs(command.ry / 128),
            step_x: (Math.floor(fromInt8(command.ly, -1, 1) * 10) / 10) * 3,
            step_z: -(Math.floor(fromInt8(command.lx, -1, 1) * 10) / 10) * 3,
            step_velocity: command.s / 128 + 1,
            step_angle: 0,
            step_depth: 0.2
        };
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

    protected body_state!: body_state_t;
    protected gait_state!: gait_state_t;
    protected dt = 0.02;

    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        this.body_state = body_state;
        this.gait_state = this.map_command(command);
        this.dt = dt / 1000;
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
    protected dt = 0.02;
    protected contact_phases = [
        [1, 0],
        [0, 1],
        [0, 1],
        [1, 0]
    ];
    protected step_length: number = 0;
    protected body_state!: body_state_t;
    protected gait_state!: gait_state_t;

    begin() {
        super.begin();
    }

    end() {
        super.end();
    }

    step(body_state: body_state_t, command: ControllerCommand, dt: number = 0.02) {
        this.body_state = body_state;
        this.gait_state = this.map_command(command);
        this.dt = dt / 1000;
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
        return contact ? this.swing(index) : this.stance(index);
    }

    swing(index: number): number[] {
        const control_points = this.get_control_points();
        const t = this.phase;
        const n = control_points.length - 1;

        const point = [0, 0, 0];
        for (let i = 0; i <= n; i++) {
            const bernstein_poly = this.comb(n, i) * Math.pow(t, i) * Math.pow(1 - t, n - i);
            point[0] += bernstein_poly * control_points[i][0];
            point[2] += bernstein_poly * control_points[i][1];
            point[1] += bernstein_poly * control_points[i][2];
        }
        this.body_state.feet[index][0] += point[0];
        if (point[0] !== 0 || point[2] !== 0) {
            this.body_state.feet[index][1] += point[1];
        }
        this.body_state.feet[index][2] += point[2];
        return this.body_state.feet[index];
    }

    stance(index: number): number[] {
        const t = this.phase;
        const L = this.step_length / 2;

        const X_POLAR = Math.cos((this.gait_state.step_z * Math.PI) / 2);
        const Y_POLAR = Math.sin((this.gait_state.step_z * Math.PI) / 2);

        const step = L * (1 - 2 * t);
        const X = step * X_POLAR;
        const Y = step * Y_POLAR;
        let Z = 0;

        if (L !== 0) {
            Z = -this.gait_state.step_depth * Math.cos((Math.PI * (X + Y)) / (2 * L));
        }

        this.body_state.feet[index][0] += X;
        this.body_state.feet[index][2] += Y;
        this.body_state.feet[index][1] += Z;

        return this.body_state.feet[index];
    }

    comb(n: number, k: number): number {
        if (k < 0 || k > n) {
            return 0;
        }
        if (k === 0 || k === n) {
            return 1;
        }
        k = Math.min(k, n - k);
        let c = 1;
        for (let i = 0; i < k; i++) {
            c = (c * (n - i)) / (i + 1);
        }
        return c;
    }

    get_control_points(): number[][] {
        const L = this.step_length / 2;
        const CH = this.gait_state.step_height;

        const STEP = [
            -L,
            -L * 1.4,
            -L * 1.5,
            -L * 1.5,
            -L * 1.5,
            0.0,
            0.0,
            0.0,
            L * 1.5,
            L * 1.5,
            L * 1.4,
            L
        ];

        const X_POLAR = Math.cos((this.gait_state.step_z * Math.PI) / 2);
        const Y_POLAR = Math.sin((this.gait_state.step_z * Math.PI) / 2);

        const control_points: number[][] = [];

        for (let i = 0; i < STEP.length; i++) {
            const X = STEP[i] * X_POLAR;
            const Y = STEP[i] * Y_POLAR;
            let Z = 0.0;

            if (i === 0 || i === 1 || i === 10 || i === 11) {
                Z = 0.0;
            } else if (i >= 2 && i <= 6) {
                Z = CH * 0.9;
            } else if (i >= 7 && i <= 9) {
                Z = CH * 1.1;
            }

            control_points.push([X, Y, Z]);
        }

        return control_points;
    }
}
