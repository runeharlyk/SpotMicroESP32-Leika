export default class Kinematic {
	private l1: number;
	private l2: number;
	private l3: number;
	private l4: number;

	private L: number;
	private W: number;

	constructor() {
		this.l1 = 50;
		this.l2 = 20;
		this.l3 = 120;
		this.l4 = 155;

		this.L = 140;
		this.W = 75;
	}

	bodyIK(
		omega: number,
		phi: number,
		psi: number,
		xm: number,
		ym: number,
		zm: number
	): number[][][] {
		const { cos, sin } = Math;

		const Rx: number[][] = [
			[1, 0, 0, 0],
			[0, cos(omega), -sin(omega), 0],
			[0, sin(omega), cos(omega), 0],
			[0, 0, 0, 1]
		];
		const Ry: number[][] = [
			[cos(phi), 0, sin(phi), 0],
			[0, 1, 0, 0],
			[-sin(phi), 0, cos(phi), 0],
			[0, 0, 0, 1]
		];
		const Rz: number[][] = [
			[cos(psi), -sin(psi), 0, 0],
			[sin(psi), cos(psi), 0, 0],
			[0, 0, 1, 0],
			[0, 0, 0, 1]
		];
		const Rxyz: number[][] = this.matrixMultiply(this.matrixMultiply(Rx, Ry), Rz);

		const T: number[][] = [
			[0, 0, 0, xm],
			[0, 0, 0, ym],
			[0, 0, 0, zm],
			[0, 0, 0, 0]
		];
		const Tm: number[][] = this.matrixAdd(T, Rxyz);

		const sHp = sin(Math.PI / 2);
		const cHp = cos(Math.PI / 2);
		const L = this.L;
		const W = this.W;

		return [
			this.matrixMultiply(Tm, [
				[cHp, 0, sHp, L / 2],
				[0, 1, 0, 0],
				[-sHp, 0, cHp, W / 2],
				[0, 0, 0, 1]
			]),
			this.matrixMultiply(Tm, [
				[cHp, 0, sHp, L / 2],
				[0, 1, 0, 0],
				[-sHp, 0, cHp, -W / 2],
				[0, 0, 0, 1]
			]),
			this.matrixMultiply(Tm, [
				[cHp, 0, sHp, -L / 2],
				[0, 1, 0, 0],
				[-sHp, 0, cHp, W / 2],
				[0, 0, 0, 1]
			]),
			this.matrixMultiply(Tm, [
				[cHp, 0, sHp, -L / 2],
				[0, 1, 0, 0],
				[-sHp, 0, cHp, -W / 2],
				[0, 0, 0, 1]
			])
		];
	}

	private legIK(point: number[]): number[] {
		const [x, y, z] = point;
		const { atan2, cos, sin, sqrt, acos } = Math;
		const { l1, l2, l3, l4 } = this;

		let F;

		try {
			F = sqrt(x ** 2 + y ** 2 - l1 ** 2);
			if (isNaN(F)) throw new Error('F is NaN');
		} catch (error) {
			//console.log(error)
			F = l1;
		}
		const G = F - l2;
		const H = sqrt(G ** 2 + z ** 2);

		const theta1 = -atan2(y, x) - atan2(F, -l1);
		const D = (H ** 2 - l3 ** 2 - l4 ** 2) / (2 * l3 * l4);
		let theta3: number;
		try {
			theta3 = acos(D);
			if (isNaN(theta3)) throw new Error('theta3 is NaN');
		} catch (error) {
			theta3 = 0;
		}
		const theta2 = atan2(z, G) - atan2(l4 * sin(theta3), l3 + l4 * cos(theta3));

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

	private matrixAdd(a: number[][], b: number[][]): number[][] {
		const result: number[][] = [];

		for (let i = 0; i < a.length; i++) {
			const row: number[] = [];

			for (let j = 0; j < a[i].length; j++) {
				row.push(a[i][j] + b[i][j]);
			}

			result.push(row);
		}

		return result;
	}

	public calcLegPoints(angles: number[]): number[][] {
		const [theta1, theta2, theta3] = angles;
		const theta23 = theta2 + theta3;

		const T0: number[] = [0, 0, 0, 1];
		const T1: number[] = this.vectorAdd(T0, [
			-this.l1 * Math.cos(theta1),
			this.l1 * Math.sin(theta1),
			0,
			0
		]);
		const T2: number[] = this.vectorAdd(T1, [
			-this.l2 * Math.sin(theta1),
			-this.l2 * Math.cos(theta1),
			0,
			0
		]);
		const T3: number[] = this.vectorAdd(T2, [
			-this.l3 * Math.sin(theta1) * Math.cos(theta2),
			-this.l3 * Math.cos(theta1) * Math.cos(theta2),
			this.l3 * Math.sin(theta2),
			0
		]);
		const T4: number[] = this.vectorAdd(T3, [
			-this.l4 * Math.sin(theta1) * Math.cos(theta23),
			-this.l4 * Math.cos(theta1) * Math.cos(theta23),
			this.l4 * Math.sin(theta23),
			0
		]);

		return [T0, T1, T2, T3, T4];
	}

	public calcIK(Lp: number[][], angles: number[], center: number[]): number[][] {
		const [omega, phi, psi] = angles;
		const [xm, ym, zm] = center;

		const [Tlf, Trf, Tlb, Trb] = this.bodyIK(omega, phi, psi, xm, ym, zm);

		const Ix: number[][] = [
			[-1, 0, 0, 0],
			[0, 1, 0, 0],
			[0, 0, 1, 0],
			[0, 0, 0, 1]
		];

		return [
			this.legIK(this.multiplyVector(this.matrixInverse(Tlf), Lp[0])),
			this.legIK(this.multiplyVector(Ix, this.multiplyVector(this.matrixInverse(Trf), Lp[1]))),
			this.legIK(this.multiplyVector(this.matrixInverse(Tlb), Lp[2])),
			this.legIK(this.multiplyVector(Ix, this.multiplyVector(this.matrixInverse(Trb), Lp[3])))
		];
	}

	private vectorAdd(a: number[], b: number[]): number[] {
		return a.map((val, index) => val + b[index]);
	}

	private matrixInverse(matrix: number[][]): number[][] {
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
