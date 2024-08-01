
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

const { cos, sin, atan2, sqrt } = Math;

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

