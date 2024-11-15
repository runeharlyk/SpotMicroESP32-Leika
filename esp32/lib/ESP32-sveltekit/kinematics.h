#ifndef Kinematics_h
#define Kinematics_h

#include <utils/math_utils.h>

struct body_state_t {
    float omega, phi, psi, xm, ym, zm;
    float feet[4][4];

    void updateFeet(const float newFeet[4][4]) { COPY_2D_ARRAY_4x4(feet, newFeet); }

    bool isEqual(const body_state_t &other) const {
        if (!IS_ALMOST_EQUAL(omega, other.omega) || !IS_ALMOST_EQUAL(phi, other.phi) ||
            !IS_ALMOST_EQUAL(psi, other.psi) || !IS_ALMOST_EQUAL(xm, other.xm) || !IS_ALMOST_EQUAL(ym, other.ym) ||
            !IS_ALMOST_EQUAL(zm, other.zm)) {
            return false;
        }
        return arrayEqual(feet, other.feet, 0.1);
    }
};

class Kinematics {
  private:
    float Trb[4][4] = {
        0,
    };
    float Trf[4][4] = {
        0,
    };
    float Tlb[4][4] = {
        0,
    };
    float Tlf[4][4] = {
        0,
    };

    const float Ix[4][4] = {{-1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    float inv[4][4];
    float point[4];
    float Q1[4][4];

    const float sHp = sinf(PI_F / 2);
    const float cHp = cosf(PI_F / 2);

    float point_lf[4][4];

    body_state_t currentState;

  public:
    float l1, l2, l3, l4;
    float L, W;

    Kinematics() {
        l1 = 60.5 / 100;
        l2 = 10 / 100;
        l3 = 111.1 / 100;
        l4 = 118.5 / 100;

        L = 207.5 / 100;
        W = 78 / 100;
    }
    ~Kinematics() {}

    esp_err_t calculate_inverse_kinematics(const body_state_t body_state, float result[12]) {
        esp_err_t ret = ESP_OK;

        if (currentState.isEqual(body_state)) return ESP_OK;

        ret = bodyIK(body_state);
        currentState.omega = body_state.omega;
        currentState.phi = body_state.phi;
        currentState.psi = body_state.psi;
        currentState.xm = body_state.xm;
        currentState.ym = body_state.ym;
        currentState.zm = body_state.zm;
        currentState.updateFeet(body_state.feet);

        ret += inverse(Tlf, inv);
        MAT_MULT(inv, body_state.feet[0], point, 4, 4, 1);
        legIK((float *)point, result);

        ret += inverse(Trf, inv);
        MAT_MULT(Ix, inv, Q1, 4, 4, 4);
        MAT_MULT(Q1, body_state.feet[1], point, 4, 4, 1);
        legIK((float *)point, result + 3);

        ret += inverse(Tlb, inv);
        MAT_MULT(inv, body_state.feet[2], point, 4, 4, 1);
        legIK((float *)point, result + 6);

        ret += inverse(Trb, inv);
        MAT_MULT(Ix, inv, Q1, 4, 4, 4);
        MAT_MULT(Q1, body_state.feet[3], point, 4, 4, 1);
        legIK((float *)point, result + 9);

        return ret;
    }

    esp_err_t bodyIK(const body_state_t p) {
        float cos_omega = COS_DEG_F(p.omega);
        float sin_omega = SIN_DEG_F(p.omega);
        float cos_phi = COS_DEG_F(p.phi);
        float sin_phi = SIN_DEG_F(p.phi);
        float cos_psi = COS_DEG_F(p.psi);
        float sin_psi = SIN_DEG_F(p.psi);

        float Tm[4][4] = {{cos_phi * cos_psi, -sin_psi * cos_phi, sin_phi, p.xm},
                          {sin_omega * sin_phi * cos_psi + sin_psi * cos_omega,
                           -sin_omega * sin_phi * sin_psi + cos_omega * cos_psi, -sin_omega * cos_phi, p.ym},
                          {sin_omega * sin_psi - sin_phi * cos_omega * cos_psi,
                           sin_omega * cos_psi + sin_phi * sin_psi * cos_omega, cos_omega * cos_phi, p.zm},
                          {0, 0, 0, 1}};

        float point_lf[4][4] = {{cHp, 0, sHp, L / 2}, {0, 1, 0, 0}, {-sHp, 0, cHp, W / 2}, {0, 0, 0, 1}};

        float point_rf[4][4] = {{cHp, 0, sHp, L / 2}, {0, 1, 0, 0}, {-sHp, 0, cHp, -W / 2}, {0, 0, 0, 1}};

        float point_lb[4][4] = {{cHp, 0, sHp, -L / 2}, {0, 1, 0, 0}, {-sHp, 0, cHp, W / 2}, {0, 0, 0, 1}};

        float point_rb[4][4] = {{cHp, 0, sHp, -L / 2}, {0, 1, 0, 0}, {-sHp, 0, cHp, -W / 2}, {0, 0, 0, 1}};

        MAT_MULT(Tm, point_lf, Tlf, 4, 4, 4);
        MAT_MULT(Tm, point_rf, Trf, 4, 4, 4);
        MAT_MULT(Tm, point_lb, Tlb, 4, 4, 4);
        MAT_MULT(Tm, point_rb, Trb, 4, 4, 4);
        return ESP_OK;
    }

    void legIK(float point[4], float result[3]) {
        float x = point[0], y = point[1], z = point[2];

        float F = sqrtf(x * x + y * y - l1 * l1);
        F = isnanf(F) ? l1 : F;
        float G = F - l2;
        float H = sqrtf(G * G + z * z);

        float theta1 = -atan2f(y, x) - atan2f(F, -l1);
        float D = (H * H - l3 * l3 - l4 * l4) / (2 * l3 * l4);
        float theta3 = atan2(sqrt(1 - D * D), D);
        if (isnan(theta3)) theta3 = 0;
        float theta2 = atan2f(z, G) - atan2f(l4 * sinf(theta3), l3 + l4 * cosf(theta3));
        result[0] = RAD_TO_DEG_F(theta1);
        result[1] = RAD_TO_DEG_F(theta2);
        result[2] = RAD_TO_DEG_F(theta3);
    }

    esp_err_t inverse(float a[4][4], float b[4][4]) {
        float s[] = {a[0][0] * a[1][1] - a[1][0] * a[0][1], a[0][0] * a[1][2] - a[1][0] * a[0][2],
                     a[0][0] * a[1][3] - a[1][0] * a[0][3], a[0][1] * a[1][2] - a[1][1] * a[0][2],
                     a[0][1] * a[1][3] - a[1][1] * a[0][3], a[0][2] * a[1][3] - a[1][2] * a[0][3]};
        float c[] = {a[2][0] * a[3][1] - a[3][0] * a[2][1], a[2][0] * a[3][2] - a[3][0] * a[2][2],
                     a[2][0] * a[3][3] - a[3][0] * a[2][3], a[2][1] * a[3][2] - a[3][1] * a[2][2],
                     a[2][1] * a[3][3] - a[3][1] * a[2][3], a[2][2] * a[3][3] - a[3][2] * a[2][3]};
        float det = s[0] * c[5] - s[1] * c[4] + s[2] * c[3] + s[3] * c[2] - s[4] * c[1] + s[5] * c[0];
        if (det == 0.0) return ESP_FAIL;
        float invdet = 1.0 / det;
        b[0][0] = (a[1][1] * c[5] - a[1][2] * c[4] + a[1][3] * c[3]) * invdet;
        b[0][1] = (-a[0][1] * c[5] + a[0][2] * c[4] - a[0][3] * c[3]) * invdet;
        b[0][2] = (a[3][1] * s[5] - a[3][2] * s[4] + a[3][3] * s[3]) * invdet;
        b[0][3] = (-a[2][1] * s[5] + a[2][2] * s[4] - a[2][3] * s[3]) * invdet;
        b[1][0] = (-a[1][0] * c[5] + a[1][2] * c[2] - a[1][3] * c[1]) * invdet;
        b[1][1] = (a[0][0] * c[5] - a[0][2] * c[2] + a[0][3] * c[1]) * invdet;
        b[1][2] = (-a[3][0] * s[5] + a[3][2] * s[2] - a[3][3] * s[1]) * invdet;
        b[1][3] = (a[2][0] * s[5] - a[2][2] * s[2] + a[2][3] * s[1]) * invdet;
        b[2][0] = (a[1][0] * c[4] - a[1][1] * c[2] + a[1][3] * c[0]) * invdet;
        b[2][1] = (-a[0][0] * c[4] + a[0][1] * c[2] - a[0][3] * c[0]) * invdet;
        b[2][2] = (a[3][0] * s[4] - a[3][1] * s[2] + a[3][3] * s[0]) * invdet;
        b[2][3] = (-a[2][0] * s[4] + a[2][1] * s[2] - a[2][3] * s[0]) * invdet;
        b[3][0] = (-a[1][0] * c[3] + a[1][1] * c[1] - a[1][2] * c[0]) * invdet;
        b[3][1] = (a[0][0] * c[3] - a[0][1] * c[1] + a[0][2] * c[0]) * invdet;
        b[3][2] = (-a[3][0] * s[3] + a[3][1] * s[1] - a[3][2] * s[0]) * invdet;
        b[3][3] = (a[2][0] * s[3] - a[2][1] * s[1] + a[2][2] * s[0]) * invdet;
        return ESP_OK;
    }
};

#endif