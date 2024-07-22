import { radToDeg } from 'three/src/math/MathUtils.js';

export interface body_state_t {
	omega: number;
	phi: number;
	psi: number;
	xm: number;
	ym: number;
	zm: number;
	feet: number[][];
}

export interface position {
	x: number;
	y: number;
	z: number;
}

export interface target_position {
	x: number;
	z: number;
	yaw: number;
}

const { cos, sin, atan2, sqrt, acos } = Math;

const DEG2RAD = 0.017453292519943;

export default class Kinematic {
	l1: number;
	l2: number;
	l3: number;
	l4: number;

	L: number;
	W: number;

	DEG2RAD = DEG2RAD;

	sHp = sin(Math.PI / 2);
	cHp = cos(Math.PI / 2);

	Tlf: number[][] = [];
	Trf: number[][] = [];
	Tlb: number[][] = [];
	Trb: number[][] = [];

	point_lf: number[][];
	point_rf: number[][];
	point_lb: number[][];
	point_rb: number[][];
	Ix: number[][];

	constructor() {
		this.l1 = 60.5 / 100;
		this.l2 = 10 / 100;
		this.l3 = 100.7 / 100;
		this.l4 = 118.5 / 100;

		this.L = 207.5 / 100;
		this.W = 78 / 100;

		this.point_lf = [
			[this.cHp, 0, this.sHp, this.L / 2],
			[0, 1, 0, 0],
			[-this.sHp, 0, this.cHp, this.W / 2],
			[0, 0, 0, 1]
		];

		this.point_rf = [
			[this.cHp, 0, this.sHp, this.L / 2],
			[0, 1, 0, 0],
			[-this.sHp, 0, this.cHp, -this.W / 2],
			[0, 0, 0, 1]
		];

		this.point_lb = [
			[this.cHp, 0, this.sHp, -this.L / 2],
			[0, 1, 0, 0],
			[-this.sHp, 0, this.cHp, this.W / 2],
			[0, 0, 0, 1]
		];

		this.point_rb = [
			[this.cHp, 0, this.sHp, -this.L / 2],
			[0, 1, 0, 0],
			[-this.sHp, 0, this.cHp, -this.W / 2],
			[0, 0, 0, 1]
		];
		this.Ix = [
			[-1, 0, 0, 0],
			[0, 1, 0, 0],
			[0, 0, 1, 0],
			[0, 0, 0, 1]
		];
	}

	public calcIK(body_state: body_state_t): number[] {
		this.bodyIK(body_state);

		return [
			...this.legIK(this.multiplyVector(this.inverse(this.Tlf), body_state.feet[0])),
			...this.legIK(
				this.multiplyVector(
					this.Ix,
					this.multiplyVector(this.inverse(this.Trf), body_state.feet[1])
				)
			),
			...this.legIK(this.multiplyVector(this.inverse(this.Tlb), body_state.feet[2])),
			...this.legIK(
				this.multiplyVector(
					this.Ix,
					this.multiplyVector(this.inverse(this.Trb), body_state.feet[3])
				)
			)
		];
	}

	bodyIK(p: body_state_t) {
		const cos_omega = cos(p.omega * this.DEG2RAD);
		const sin_omega = sin(p.omega * this.DEG2RAD);
		const cos_phi = cos(p.phi * this.DEG2RAD);
		const sin_phi = sin(p.phi * this.DEG2RAD);
		const cos_psi = cos(p.psi * this.DEG2RAD);
		const sin_psi = sin(p.psi * this.DEG2RAD);

		const Tm: number[][] = [
			[cos_phi * cos_psi, -sin_psi * cos_phi, sin_phi, p.xm],
			[
				sin_omega * sin_phi * cos_psi + sin_psi * cos_omega,
				-sin_omega * sin_phi * sin_psi + cos_omega * cos_psi,
				-sin_omega * cos_phi,
				p.ym
			],
			[
				sin_omega * sin_psi - sin_phi * cos_omega * cos_psi,
				sin_omega * cos_psi + sin_phi * sin_psi * cos_omega,
				cos_omega * cos_phi,
				p.zm
			],
			[0, 0, 0, 1]
		];

		this.Tlf = this.matrixMultiply(Tm, this.point_lf);
		this.Trf = this.matrixMultiply(Tm, this.point_rf);
		this.Tlb = this.matrixMultiply(Tm, this.point_lb);
		this.Trb = this.matrixMultiply(Tm, this.point_rb);
	}

	public legIK(point: number[]): number[] {
		const [x, y, z] = point;

		let F = sqrt(x ** 2 + y ** 2 - this.l1 ** 2);
		if (isNaN(F)) F = this.l1;

		const G = F - this.l2;
		const H = sqrt(G ** 2 + z ** 2);

		const theta1 = -atan2(y, x) - atan2(F, -this.l1);
		const D = (H ** 2 - this.l3 ** 2 - this.l4 ** 2) / (2 * this.l3 * this.l4);
		let theta3 = atan2(sqrt(1 - D ** 2), D);
		if (isNaN(theta3)) theta3 = 0;

		const theta2 = atan2(z, G) - atan2(this.l4 * sin(theta3), this.l3 + this.l4 * cos(theta3));

		return [theta1, theta2, theta3];
	}

	matrixMultiply(a: number[][], b: number[][]): number[][] {
		const result: number[][] = [];

		for (let i = 0; i < a.length; i++) {
			const row: number[] = [];

			for (let j = 0; j < b[0].length; j++) {
				let sum = 0;

				for (let k = 0; k < a[i].length; k++) {
					sum += a[i][k] * b[k][j];
				}

				row.push(sum);
			}

			result.push(row);
		}

		return result;
	}

	multiplyVector(matrix: number[][], vector: number[]): number[] {
		const rows = matrix.length;
		const cols = matrix[0].length;
		const vectorLength = vector.length;

		if (cols !== vectorLength) {
			throw new Error('Matrix and vector dimensions do not match for multiplication.');
		}

		const result = [];

		for (let i = 0; i < rows; i++) {
			let sum = 0;

			for (let j = 0; j < cols; j++) {
				sum += matrix[i][j] * vector[j];
			}

			result.push(sum);
		}

		return result;
	}

	private inverse(matrix: number[][]): number[][] {
		const det = this.determinant(matrix);
		const adjugate = this.adjugate(matrix);
		const scalar = 1 / det;
		const inverse: number[][] = [];

		for (let i = 0; i < matrix.length; i++) {
			const row: number[] = [];

			for (let j = 0; j < matrix[i].length; j++) {
				row.push(adjugate[i][j] * scalar);
			}

			inverse.push(row);
		}

		return inverse;
	}

	private determinant(matrix: number[][]): number {
		if (matrix.length !== matrix[0].length) {
			throw new Error('The matrix is not square.');
		}

		if (matrix.length === 2) {
			return matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0];
		}

		let det = 0;

		for (let i = 0; i < matrix.length; i++) {
			const sign = i % 2 === 0 ? 1 : -1;
			const subMatrix: number[][] = [];

			for (let j = 1; j < matrix.length; j++) {
				const row: number[] = [];

				for (let k = 0; k < matrix.length; k++) {
					if (k !== i) {
						row.push(matrix[j][k]);
					}
				}

				subMatrix.push(row);
			}

			det += sign * matrix[0][i] * this.determinant(subMatrix);
		}

		return det;
	}

	private adjugate(matrix: number[][]): number[][] {
		if (matrix.length !== matrix[0].length) {
			throw new Error('The matrix is not square.');
		}

		const adjugate: number[][] = [];

		for (let i = 0; i < matrix.length; i++) {
			const row: number[] = [];

			for (let j = 0; j < matrix[i].length; j++) {
				const sign = (i + j) % 2 === 0 ? 1 : -1;
				const subMatrix: number[][] = [];

				for (let k = 0; k < matrix.length; k++) {
					if (k !== i) {
						const subRow: number[] = [];

						for (let l = 0; l < matrix.length; l++) {
							if (l !== j) {
								subRow.push(matrix[k][l]);
							}
						}

						subMatrix.push(subRow);
					}
				}

				const cofactor = sign * this.determinant(subMatrix);
				row.push(cofactor);
			}

			adjugate.push(row);
		}

		return this.transpose(adjugate);
	}

	private transpose(matrix: number[][]): number[][] {
		const transposed: number[][] = [];

		for (let i = 0; i < matrix.length; i++) {
			const row: number[] = [];

			for (let j = 0; j < matrix[i].length; j++) {
				row.push(matrix[j][i]);
			}

			transposed.push(row);
		}

		return transposed;
	}
}

const swing_time = 0.36;
const overlap_time = 0.0;
const dt = 0.02;
const swing_ticks = Math.round(swing_time / dt);

const num_phases = 4;
const stance_ticks = 7 * swing_ticks;
const overlap_ticks = Math.round(overlap_time / dt);

const phase_ticks = new Array(4).fill(swing_ticks);
const phase_length = num_phases * swing_ticks;

let rb_contact_phases = [1, 0, 1, 1];
let rf_contact_phases = [1, 1, 1, 0];
let lf_contact_phases = [1, 0, 1, 1];
let lb_contact_phases = [1, 1, 1, 0];

export class Command {
	public x_vel_cmd_mps = 0;
	public y_vel_cmd_mps = 0;
	public yaw_rate_cmd_rps = 0;
	public phi_cmd = 0;
	public theta_cmd = 0;
	public psi_cmd = 0;
	public idle_cmd = false;
	public walk_cmd = false;
	public stand_cmd = false;
	constructor() {}

	resetCommands() {
		this.x_vel_cmd_mps = 0;
		this.y_vel_cmd_mps = 0;
		this.yaw_rate_cmd_rps = 0;
		this.phi_cmd = 0;
		this.theta_cmd = 0;
		this.psi_cmd = 0;
		this.idle_cmd = false;
		this.walk_cmd = false;
		this.stand_cmd = false;
	}

	getXSpeedCmd() {
		return this.x_vel_cmd_mps;
	}
	getYSpeedCmd() {
		return this.y_vel_cmd_mps;
	}
	getYawRateCmd() {
		return this.yaw_rate_cmd_rps;
	}
}

const cmd = new Command();

export class GaitState {
	protected name: string;
	protected static body_state: body_state_t;
	constructor() {
		this.name = 'GaitState';
	}

	begin() {
		console.log('Starting', this.name);
	}
	end() {
		console.log('Ending', this.name);
	}
	step(dt: number) {
		console.log('Stepping', this.name);
	}

	getNeutralStance() {
		return [
			[100, -100, 100, 1],
			[100, -100, -100, 1],
			[-100, -100, 100, 1],
			[-100, -100, -100, 1]
		];
	}

	getDefaultStandHeight = () => 70;
}

export class IdleState extends GaitState {
	constructor() {
		super();
		this.name = 'Idle';
	}

	begin() {
		console.log('Starting', this.name);
	}
}

export class StandState extends GaitState {
	constructor() {
		super();
		this.name = 'Stand';
	}
}

export class WalkState extends GaitState {
	num_phases = 4;
	ticks = 0;
	constructor() {
		super();
		this.name = 'Walk';
	}

	begin() {
		super.begin();
		WalkState.body_state.feet = this.getNeutralStance();
		WalkState.body_state.omega = 0;
		WalkState.body_state.phi = 0;
		WalkState.body_state.psi = 0;
		WalkState.body_state.xm = 0;
		WalkState.body_state.ym = this.getDefaultStandHeight();
		WalkState.body_state.zm = 0;
	}
	end() {
		super.end();
	}
	step(dt: number) {
		super.end();
		this.updatePhaseData();
		WalkState.body_state.feet = this.stepGait();
		if (this.num_phases == 8) {
			const [omega, phi, psi] = this.stepBodyShift();
			WalkState.body_state.omega = omega;
			WalkState.body_state.phi = phi;
			WalkState.body_state.psi = psi;
		}
		this.ticks++;
	}

	updatePhaseData() {}

	stepGait() {
		let contact_mode;
		let swing_proportion;
		let foot_pos;
		let new_foot_pos;
		let default_stance_feet_pos = this.getNeutralStance();
	}

	stepBodyShift() {
		let omega = 0;
		let phi = 0;
		let psi = 0;
		return [omega, phi, psi];
	}
}

export class TrotState extends GaitState {
	constructor() {
		super();
		this.name = 'Trot';
	}
}

const smnc = {
	alpha: 0.5,
	beta: 0.5,
	foot_height_time_constant: 0.02,
	z_clearance: 0.05
};

export class GaitPlanner {
	gaitCycleDuration = 10;
	time = 0;
	stepHeight = 30;
	stepLength = 75;
	num_phases = 4;
	gaitCycle = 10;
	phaseOffset = Math.PI;

	ticks_ = 0;
	phase_index_ = 0;
	subphase_ticks_ = 0;
	contact_feet_states_ = [false, false, false, false];
	default_stance_feet_pos: number[][];

	private phase: number;
	private strideLength: number;
	private height: number;
	private cyclePeriod: number;
	contact_feet_states: {
		right_back_in_swing: boolean;
		right_front_in_swing: boolean;
		left_front_in_swing: boolean;
		left_back_in_swing: boolean;
	};

	constructor() {
		let l1 = 50;
		let l2 = 20;
		let l3 = 120;
		let l4 = 155;

		let L = 140;
		let W = 75;

		this.default_stance_feet_pos = [
			[100, -100, 100, 1],
			[100, -100, -100, 1],
			[-W, -100, 100, 1],
			[-W, -100, -100, 1]
		];

		this.strideLength = 2;
		this.height = 50;
		this.cyclePeriod = 10;
		this.phase = 0;

		this.contact_feet_states = {
			right_back_in_swing: false,
			right_front_in_swing: false,
			left_front_in_swing: false,
			left_back_in_swing: false
		};
	}

	public step(bodyState: body_state_t, dt: number) {
		this.updatePhase(dt);
		this.updateFootPosition(bodyState);
		// this.UpdateBodyShift(bodyState);
		this.ticks_ += 1;
	}

	updatePhase(dt: number) {
		const phase_time = this.ticks_ % phase_length;
		let phase_sum = 0;

		for (let i = 0; i < num_phases; i++) {
			phase_sum += phase_ticks[i];
			if (phase_time < phase_sum) {
				this.phase_index_ = i;
				this.subphase_ticks_ = phase_time - phase_sum + phase_ticks[i];
				break;
			}
		}

		this.contact_feet_states.right_back_in_swing = rb_contact_phases[this.phase_index_] === 0;
		this.contact_feet_states.right_front_in_swing = rf_contact_phases[this.phase_index_] === 0;
		this.contact_feet_states.left_front_in_swing = lf_contact_phases[this.phase_index_] === 0;
		this.contact_feet_states.left_back_in_swing = lb_contact_phases[this.phase_index_] === 0;
	}

	updateFootPosition(body_state: body_state_t) {
		const contacts = [
			this.contact_feet_states.right_back_in_swing,
			this.contact_feet_states.right_front_in_swing,
			this.contact_feet_states.left_front_in_swing,
			this.contact_feet_states.left_back_in_swing
		];
		for (let i = 0; i < 4; i++) {
			let contact_mode = contacts[i];

			contact_mode
				? this.stanceController(body_state.feet[i])
				: this.swingLegController(body_state.feet[i], this.default_stance_feet_pos[i]);
		}
	}

	UpdateBodyShift(bodyState: body_state_t) {}

	stanceController(foot_pos: number[]) {
		const dt = 0.2;
		const h_tau = 0.2;
		const delta_pos = [-1 * dt, (1.0 / h_tau) * (0.0 - foot_pos[1]) * dt, -0 * dt];

		const new_foot_pos = [
			foot_pos[0] + delta_pos[0],
			foot_pos[1] + delta_pos[1],
			foot_pos[2] + delta_pos[2]
		];

		const yaw_angle = 0 * dt;
		const cos_yaw = Math.cos(yaw_angle);
		const sin_yaw = Math.sin(yaw_angle);

		const rotated_x = cos_yaw * new_foot_pos[0] - sin_yaw * new_foot_pos[2];
		const rotated_z = sin_yaw * new_foot_pos[0] + cos_yaw * new_foot_pos[2];

		foot_pos[0] = rotated_x;
		foot_pos[1] = new_foot_pos[1];
		foot_pos[2] = rotated_z;
	}

	swingLegController(foot_pos: number[], default_stance_foot_pos: number[]) {}
}

export class BezierGaitPlanner {
	private _frame: number[][];
	private _phi: number;
	private _phi_stance: number;
	private _last_time: number;
	private _alpha: number;
	private _s: boolean;
	private _offset: number[];
	private step_offset: number;

	constructor(mode: string) {
		this._frame = Array.from({ length: 4 }, () => Array(3).fill(0));
		this._phi = 0;
		this._phi_stance = 0;
		this._last_time = 0;
		this._alpha = 0;
		this._s = false;
		if (mode === 'walk') {
			this._offset = [0, 0.5, 0.5, 0];
			this.step_offset = 0.5;
		} else {
			this._offset = [0, 0, 0.8, 0.8];
			this.step_offset = 0.5;
		}
	}

	private static solve_bin_factor(n: number, k: number): number {
		return Number(this.factorial(n) / (this.factorial(k) * this.factorial(n - k)));
	}

	private bezier_curve(t: number, k: number, point: number): number {
		const n = 11;
		return (
			point * BezierGaitPlanner.solve_bin_factor(n, k) * Math.pow(t, k) * Math.pow(1 - t, n - k)
		);
	}

	private static calculate_stance(phi_st: number, v: number, angle: number): number[] {
		const c = Math.cos(angle * DEG2RAD);
		const s = Math.sin(angle * DEG2RAD);
		const A = 0.001;
		const half_l = 0.05;
		const p_stance = half_l * (1 - 2 * phi_st);
		const stance_x = c * p_stance * Math.abs(v);
		const stance_y = -s * p_stance * Math.abs(v);
		const stance_z = -A * Math.cos((Math.PI / (2 * half_l)) * p_stance);
		return [stance_x, stance_y, stance_z];
	}

	private calculate_bezier_swing(
		phi_sw: number,
		v: number,
		angle: number,
		direction: number
	): number[] {
		const c = Math.cos((angle * Math.PI) / 180);
		const s = Math.sin((angle * Math.PI) / 180);
		const X = [-0.04, -0.056, -0.06, -0.06, -0.06, 0, 0, 0, 0.06, 0.06, 0.056, 0.04].map(
			(x) => Math.abs(v) * c * x * direction
		);
		const Y = X.map((x) => Math.abs(v) * s * -x);
		const Z = [0, 0, 0.0405, 0.0405, 0.0405, 0.0405, 0.0405, 0.0495, 0.0495, 0.0495, 0, 0].map(
			(x) => Math.abs(v) * x
		);
		let swing_x = 0,
			swing_y = 0,
			swing_z = 0;
		for (let i = 0; i < 10; i++) {
			swing_x += this.bezier_curve(phi_sw, i, X[i]);
			swing_y += this.bezier_curve(phi_sw, i, Y[i]);
			swing_z += this.bezier_curve(phi_sw, i, Z[i]);
		}
		return [swing_x, swing_y, swing_z];
	}

	step_trajectory(
		phi: number,
		v: number,
		angle: number,
		w_rot: number,
		center_to_foot: number[],
		direction: number
	) {
		if (phi >= 1) phi -= 1;
		const r = Math.sqrt(center_to_foot[0] ** 2 + center_to_foot[1] ** 2);
		const foot_angle = Math.atan2(center_to_foot[1], center_to_foot[0]);
		let circle_trajectory;
		if (w_rot >= 0) {
			circle_trajectory = 90 - ((foot_angle - this._alpha) * 180) / Math.PI;
		} else {
			circle_trajectory = 270 - ((foot_angle - this._alpha) * 180) / Math.PI;
		}

		let stepX_long, stepY_long, stepZ_long, stepX_rot, stepY_rot, stepZ_rot;
		if (phi <= this.step_offset) {
			const phi_stance = phi / this.step_offset;
			[stepX_long, stepY_long, stepZ_long] = BezierGaitPlanner.calculate_stance(
				phi_stance,
				v,
				angle
			);
			[stepX_rot, stepY_rot, stepZ_rot] = BezierGaitPlanner.calculate_stance(
				phi_stance,
				w_rot,
				circle_trajectory
			);
		} else {
			const phiSwing = (phi - this.step_offset) / (1 - this.step_offset);
			[stepX_long, stepY_long, stepZ_long] = this.calculate_bezier_swing(
				phiSwing,
				v,
				angle,
				direction
			);
			[stepX_rot, stepY_rot, stepZ_rot] = this.calculate_bezier_swing(
				phiSwing,
				w_rot,
				circle_trajectory,
				direction
			);
		}

		if (center_to_foot[1] > 0) {
			this._alpha =
				stepX_rot < 0
					? -Math.atan2(Math.sqrt(stepX_rot ** 2 + stepY_rot ** 2), r)
					: Math.atan2(Math.sqrt(stepX_rot ** 2 + stepY_rot ** 2), r);
		} else {
			this._alpha =
				stepX_rot < 0
					? Math.atan2(Math.sqrt(stepX_rot ** 2 + stepY_rot ** 2), r)
					: -Math.atan2(Math.sqrt(stepX_rot ** 2 + stepY_rot ** 2), r);
		}

		return [stepX_long + stepX_rot, stepY_long + stepY_rot, stepZ_long + stepZ_rot];
	}

	loop(v: number, angle: number, w_rot: number, t: number, direction: number, frames: number[][]) {
		if (t <= 0.01) t = 0.01;
		if (this._phi >= 0.99) this._last_time = Date.now() / 1000;
		this._phi = (Date.now() / 1000 - this._last_time) / t;

		for (let i = 0; i < 4; i++) {
			const step_coord = this.step_trajectory(
				this._phi + this._offset[i],
				v,
				angle,
				w_rot,
				frames[i],
				direction
			);
			this._frame[i] = [
				frames[i][0] + step_coord[0],
				frames[i][1] + step_coord[1],
				frames[i][2] + step_coord[2],
				1
			];
		}

		return this._frame;
	}

	private static factorial = (function () {
		const cache = [1n, 1n];
		let i = 2;

		return function (n: number) {
			if (cache[n] !== undefined) return cache[n];
			for (; i <= n; i++) {
				cache[i] = cache[i - 1] * BigInt(i);
			}
			return cache[n];
		};
	})();
}
