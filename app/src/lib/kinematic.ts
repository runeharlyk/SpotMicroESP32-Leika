export interface position_t {
	omega: number;
	phi: number;
	psi: number;
	xm: number;
	ym: number;
	zm: number;
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

	public calcIK(Lp: number[][], position: position_t): number[][] {
		this.bodyIK(position);

		return [
			this.legIK(this.multiplyVector(this.inverse(this.Tlf), Lp[0])),
			this.legIK(this.multiplyVector(this.Ix, this.multiplyVector(this.inverse(this.Trf), Lp[1]))),
			this.legIK(this.multiplyVector(this.inverse(this.Tlb), Lp[2])),
			this.legIK(this.multiplyVector(this.Ix, this.multiplyVector(this.inverse(this.Trb), Lp[3])))
		];
	}

	bodyIK(p: position_t) {
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

export class ForwardKinematics {
	private l1: number;
	private l2: number;
	private l3: number;
	private l4: number;

	constructor() {
		this.l1 = 50;
		this.l2 = 20;
		this.l3 = 120;
		this.l4 = 155;
	}

	public calculateFootpoint(theta1: number, theta2: number, theta3: number): number[] {
		const { cos, sin } = Math;

		const x =
			this.l1 * cos(theta1) +
			this.l2 * cos(theta1) +
			this.l3 * cos(theta1 + theta2) +
			this.l4 * cos(theta1 + theta2 + theta3);
		const y =
			this.l1 * sin(theta1) +
			this.l2 * sin(theta1) +
			this.l3 * sin(theta1 + theta2) +
			this.l4 * sin(theta1 + theta2 + theta3);
		const z = 0;

		return [x, y, z];
	}

	public calculateFootpoints(angles: number[]): number[][] {
		const footpoints: number[][] = [];

		for (let i = 0; i < angles.length; i += 3) {
			const theta1 = angles[i];
			const theta2 = angles[i + 1];
			const theta3 = angles[i + 2];
			const footpoint = this.calculateFootpoint(theta1, theta2, theta3);
			footpoints.push(footpoint);
		}

		return footpoints;
	}
}
