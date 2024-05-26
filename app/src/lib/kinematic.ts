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

export default class Kinematic {
	l1: number;
	l2: number;
	l3: number;
	l4: number;

	L: number;
	W: number;

	DEGREES2RAD = 0.017453292519943;

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
		this.l1 = 50;
		this.l2 = 20;
		this.l3 = 120;
		this.l4 = 155;

		this.L = 140;
		this.W = 75;

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
		const cos_omega = cos(p.omega * this.DEGREES2RAD);
		const sin_omega = sin(p.omega * this.DEGREES2RAD);
		const cos_phi = cos(p.phi * this.DEGREES2RAD);
		const sin_phi = sin(p.phi * this.DEGREES2RAD);
		const cos_psi = cos(p.psi * this.DEGREES2RAD);
		const sin_psi = sin(p.psi * this.DEGREES2RAD);

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

	private legIK(point: number[]): number[] {
		const [x, y, z] = point;

		let F = sqrt(x ** 2 + y ** 2 - this.l1 ** 2);
		if (isNaN(F)) F = this.l1;

		const G = F - this.l2;
		const H = sqrt(G ** 2 + z ** 2);

		const theta1 = -atan2(y, x) - atan2(F, -this.l1);
		const D = (H ** 2 - this.l3 ** 2 - this.l4 ** 2) / (2 * this.l3 * this.l4);
		let theta3 = acos(D);
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
	}

	public step(bodyState: body_state_t, dt: number) {
		this.updatePhase(dt);
		this.updateFootPosition(bodyState);
		this.UpdateBodyShift(bodyState);
	}

	updatePhase(dt: number) {
		this.time += dt;

		this.ticks_++;
		let phase_time = this.ticks_ % phase_length;
	}

	updateFootPosition(body_state: body_state_t) {
		for (let i = 0; i < 4; i++) {
			let contact_mode = this.contact_feet_states_[i];

			body_state.feet[i] = contact_mode
				? this.stanceController(body_state.feet[i])
				: this.swingLegController(body_state.feet[i], this.default_stance_feet_pos[i]);
		}
	}

	UpdateBodyShift(bodyState: body_state_t) {}

	stanceController(foot_pos: number[]) {
		foot_pos[0] = -100;
		foot_pos[1] = sin(radToDeg(this.ticks_) / 2 + Math.PI / 2) * 100;
		return foot_pos;
	}

	swingLegController(foot_pos: number[], default_stance_foot_pos: number[]) {
		let swing_proportion = this.subphase_ticks_ / swing_ticks;
		// foot_pos[0] = default_stance_foot_pos[0];
		foot_pos[1] = default_stance_foot_pos[1] + 100;
		// foot_pos[2] = default_stance_foot_pos[2];
		// foot_pos[0] = cos(this.time / 2) * 50;
		// foot_pos[1] = default_stance_foot_pos[1] - sin(this.time / 2 + Math.PI / 2) * 50;
		return foot_pos;
	}
}
