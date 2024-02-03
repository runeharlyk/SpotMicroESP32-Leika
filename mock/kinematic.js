export default class Kinematic {
  l1;
  l2;
  l3;
  l4;

  L;
  W;

  constructor() {
    this.l1 = 50;
    this.l2 = 20;
    this.l3 = 120;
    this.l4 = 155;

    this.L = 140;
    this.W = 75;
  }

  bodyIK(omega, phi, psi, xm, ym, zm) {
    const { cos, sin } = Math;

    const Rx = [
      [1, 0, 0, 0],
      [0, cos(omega), -sin(omega), 0],
      [0, sin(omega), cos(omega), 0],
      [0, 0, 0, 1],
    ];
    const Ry = [
      [cos(phi), 0, sin(phi), 0],
      [0, 1, 0, 0],
      [-sin(phi), 0, cos(phi), 0],
      [0, 0, 0, 1],
    ];
    const Rz = [
      [cos(psi), -sin(psi), 0, 0],
      [sin(psi), cos(psi), 0, 0],
      [0, 0, 1, 0],
      [0, 0, 0, 1],
    ];
    const Rxyz = this.matrixMultiply(this.matrixMultiply(Rx, Ry), Rz);

    const T = [
      [0, 0, 0, xm],
      [0, 0, 0, ym],
      [0, 0, 0, zm],
      [0, 0, 0, 0],
    ];
    const Tm = this.matrixAdd(T, Rxyz);

    const sHp = sin(Math.PI / 2);
    const cHp = cos(Math.PI / 2);

    return [
      this.matrixMultiply(Tm, [
        [cHp, 0, sHp, this.L / 2],
        [0, 1, 0, 0],
        [-sHp, 0, cHp, this.W / 2],
        [0, 0, 0, 1],
      ]),
      this.matrixMultiply(Tm, [
        [cHp, 0, sHp, this.L / 2],
        [0, 1, 0, 0],
        [-sHp, 0, cHp, -this.W / 2],
        [0, 0, 0, 1],
      ]),
      this.matrixMultiply(Tm, [
        [cHp, 0, sHp, -this.L / 2],
        [0, 1, 0, 0],
        [-sHp, 0, cHp, this.W / 2],
        [0, 0, 0, 1],
      ]),
      this.matrixMultiply(Tm, [
        [cHp, 0, sHp, -this.L / 2],
        [0, 1, 0, 0],
        [-sHp, 0, cHp, -this.W / 2],
        [0, 0, 0, 1],
      ]),
    ];
  }

  legIK(point) {
    const [x, y, z] = point;
    const { atan2, cos, sin, sqrt, acos } = Math;

    let F;

    try {
      F = sqrt(x ** 2 + y ** 2 - this.l1 ** 2);
      if (isNaN(F)) throw new Error("F is NaN");
    } catch (error) {
      F = this.l1;
    }
    const G = F - this.l2;
    const H = sqrt(G ** 2 + z ** 2);

    const theta1 = -atan2(y, x) - atan2(F, -this.l1);
    let theta3;
    try {
      theta3 = acos(
        (H ** 2 - this.l3 ** 2 - this.l4 ** 2) / (2 * this.l3 * this.l4)
      );
      if (isNaN(theta3)) throw new Error("theta3 is NaN");
    } catch (error) {
      theta3 = 0;
    }
    const theta2 =
      atan2(z, G) -
      atan2(this.l4 * sin(theta3), this.l3 + this.l4 * cos(theta3));

    return [theta1, theta2, theta3];
  }

  matrixMultiply(a, b) {
    const result = [];

    for (let i = 0; i < a.length; i++) {
      const row = [];

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

  multiplyVector(matrix, vector) {
    const rows = matrix.length;
    const cols = matrix[0].length;
    const vectorLength = vector.length;

    if (cols !== vectorLength) {
      throw new Error(
        "Matrix and vector dimensions do not match for multiplication."
      );
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

  matrixAdd(a, b) {
    const result = [];

    for (let i = 0; i < a.length; i++) {
      const row = [];

      for (let j = 0; j < a[i].length; j++) {
        row.push(a[i][j] + b[i][j]);
      }

      result.push(row);
    }

    return result;
  }

  calcLegPoints(angles) {
    const [theta1, theta2, theta3] = angles;
    const theta23 = theta2 + theta3;

    const T0 = [0, 0, 0, 1];
    const T1 = this.vectorAdd(T0, [
      -this.l1 * Math.cos(theta1),
      this.l1 * Math.sin(theta1),
      0,
      0,
    ]);
    const T2 = this.vectorAdd(T1, [
      -this.l2 * Math.sin(theta1),
      -this.l2 * Math.cos(theta1),
      0,
      0,
    ]);
    const T3 = this.vectorAdd(T2, [
      -this.l3 * Math.sin(theta1) * Math.cos(theta2),
      -this.l3 * Math.cos(theta1) * Math.cos(theta2),
      this.l3 * Math.sin(theta2),
      0,
    ]);
    const T4 = this.vectorAdd(T3, [
      -this.l4 * Math.sin(theta1) * Math.cos(theta23),
      -this.l4 * Math.cos(theta1) * Math.cos(theta23),
      this.l4 * Math.sin(theta23),
      0,
    ]);

    return [T0, T1, T2, T3, T4];
  }

  calcIK(Lp, angles, center) {
    const [omega, phi, psi] = angles;
    const [xm, ym, zm] = center;

    const [Tlf, Trf, Tlb, Trb] = this.bodyIK(omega, phi, psi, xm, ym, zm);

    const Ix = [
      [-1, 0, 0, 0],
      [0, 1, 0, 0],
      [0, 0, 1, 0],
      [0, 0, 0, 1],
    ];

    return [
      this.legIK(this.multiplyVector(this.matrixInverse(Tlf), Lp[0])),
      this.legIK(
        this.multiplyVector(
          Ix,
          this.multiplyVector(this.matrixInverse(Trf), Lp[1])
        )
      ),
      this.legIK(this.multiplyVector(this.matrixInverse(Tlb), Lp[2])),
      this.legIK(
        this.multiplyVector(
          Ix,
          this.multiplyVector(this.matrixInverse(Trb), Lp[3])
        )
      ),
    ];
  }

  vectorAdd(a, b) {
    return a.map((val, index) => val + b[index]);
  }

  matrixInverse(matrix) {
    const det = this.determinant(matrix);
    const adjugate = this.adjugate(matrix);
    const scalar = 1 / det;
    const inverse = [];

    for (let i = 0; i < matrix.length; i++) {
      const row = [];

      for (let j = 0; j < matrix[i].length; j++) {
        row.push(adjugate[i][j] * scalar);
      }

      inverse.push(row);
    }

    return inverse;
  }

  determinant(matrix) {
    if (matrix.length !== matrix[0].length) {
      throw new Error("The matrix is not square.");
    }

    if (matrix.length === 2) {
      return matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0];
    }

    let det = 0;

    for (let i = 0; i < matrix.length; i++) {
      const sign = i % 2 === 0 ? 1 : -1;
      const subMatrix = [];

      for (let j = 1; j < matrix.length; j++) {
        const row = [];

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

  adjugate(matrix) {
    if (matrix.length !== matrix[0].length) {
      throw new Error("The matrix is not square.");
    }

    const adjugate = [];

    for (let i = 0; i < matrix.length; i++) {
      const row = [];

      for (let j = 0; j < matrix[i].length; j++) {
        const sign = (i + j) % 2 === 0 ? 1 : -1;
        const subMatrix = [];

        for (let k = 0; k < matrix.length; k++) {
          if (k !== i) {
            const subRow = [];

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

  transpose(matrix) {
    const transposed = [];

    for (let i = 0; i < matrix.length; i++) {
      const row = [];

      for (let j = 0; j < matrix[i].length; j++) {
        row.push(matrix[j][i]);
      }

      transposed.push(row);
    }

    return transposed;
  }
}

class ForwardKinematics {
  l1;
  l2;
  l3;
  l4;

  constructor() {
    this.l1 = 50;
    this.l2 = 20;
    this.l3 = 120;
    this.l4 = 155;
  }

  calculateFootpoint(theta1, theta2, theta3) {
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

  calculateFootpoints(angles) {
    const footpoints = [];

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
