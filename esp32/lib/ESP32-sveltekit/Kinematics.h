#ifndef Kinematics_h
#define Kinematics_h

#include <esp_dsp.h>
#include <cmath>

#define RAD2DEG 57.295779513082321 // 180 / PI
#define DEG2RAD 0.017453292519943

typedef struct {
    float omega, phi, psi, xm, ym, zm;
    float feet[4][4];

    void updateFeet(const float newFeet[4][4]) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                feet[i][j] = newFeet[i][j];
            }
        }
    }
} body_state_t;

class Kinematics
{
private:
    float Trb[4][4] = {0,};
    float Trf[4][4] = {0,};
    float Tlb[4][4] = {0,};
    float Tlf[4][4] = {0,};

    const float Ix[4][4] = {{-1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    float inv[4][4];
    float point[4];
    float Q1[4][4];

    const float sHp = sin(PI / 2);
    const float cHp = cos(PI / 2);

    float point_lf[4][4];

   public:
    float l1, l2, l3, l4;
    float L, W;

    Kinematics() {
        l1 = 50;
        l2 = 20;
        l3 = 120;
        l4 = 155;

        L = 140;
        W = 75;
    }
    ~Kinematics() {}

    esp_err_t calculate_inverse_kinematics(body_state_t body_state, float result[12]) {
        esp_err_t ret = ESP_OK;

        ret = bodyIK(body_state);

        ret += inverse(Tlf, inv);
        dspm_mult_f32_ae32((float*) inv, (float*) body_state.feet[0], (float*) point, 4, 4, 1);
        legIK((float*) point, result);

        ret += inverse(Trf, inv);
        dspm_mult_f32_ae32((float*) Ix, (float*) inv, (float*) Q1, 4, 4, 4);
        dspm_mult_f32_ae32((float*) Q1, (float*) body_state.feet[1], (float*) point, 4, 4, 1);
        legIK((float*) point, result + 3);

        ret += inverse(Tlb, inv);
        dspm_mult_f32_ae32((float*) inv, (float*) body_state.feet[2], (float*) point, 4, 4, 1);
        legIK((float*) point, result + 6);

        ret += inverse(Trb, inv);
        dspm_mult_f32_ae32((float*) Ix, (float*) inv, (float*) Q1, 4, 4, 4);
        dspm_mult_f32_ae32((float*) Q1, (float*) body_state.feet[3], (float*) point, 4, 4, 1);
        legIK((float*) point, result + 9);

        return ret;
    }

    esp_err_t bodyIK(body_state_t p) {
        float cos_omega = cos(p.omega * DEG2RAD);
        float sin_omega = sin(p.omega * DEG2RAD);
        float cos_phi = cos(p.phi * DEG2RAD);
        float sin_phi = sin(p.phi * DEG2RAD);
        float cos_psi = cos(p.psi * DEG2RAD);
        float sin_psi = sin(p.psi * DEG2RAD);

        float Tm[4][4] = {
            {cos_phi * cos_psi, -sin_psi * cos_phi, sin_phi, p.xm},
            {sin_omega * sin_phi * cos_psi + sin_psi * cos_omega,
             -sin_omega * sin_phi * sin_psi + cos_omega * cos_psi,
             -sin_omega * cos_phi, p.ym},
            {sin_omega * sin_psi - sin_phi * cos_omega * cos_psi,
             sin_omega * cos_psi + sin_phi * sin_psi * cos_omega,
             cos_omega * cos_phi, p.zm},
            {0, 0, 0, 1}};

        float point_lf[4][4] = {{cHp, 0, sHp, L / 2},
                                {0, 1, 0, 0},
                                {-sHp, 0, cHp, W / 2},
                                {0, 0, 0, 1}};

        float point_rf[4][4] = {{cHp, 0, sHp, L / 2},
                                {0, 1, 0, 0},
                                {-sHp, 0, cHp, -W / 2},
                                {0, 0, 0, 1}};

        float point_lb[4][4] = {{cHp, 0, sHp, -L / 2},
                                {0, 1, 0, 0},
                                {-sHp, 0, cHp, W / 2},
                                {0, 0, 0, 1}};

        float point_rb[4][4] = {{cHp, 0, sHp, -L / 2},
                                {0, 1, 0, 0},
                                {-sHp, 0, cHp, -W / 2},
                                {0, 0, 0, 1}};

        dspm_mult_f32_ae32((float*)Tm, (float*)point_lf, (float*)Tlf, 4, 4, 4);
        dspm_mult_f32_ae32((float*)Tm, (float*)point_rf, (float*)Trf, 4, 4, 4);
        dspm_mult_f32_ae32((float*)Tm, (float*)point_lb, (float*)Tlb, 4, 4, 4);
        dspm_mult_f32_ae32((float*)Tm, (float*)point_rb, (float*)Trb, 4, 4, 4);
        return ESP_OK;
    }

    void legIK(float point[4], float result[3]) {
        float x = point[0], y = point[1], z = point[2];

        float F = sqrt(x * x + y * y - l1 * l1);
        F = isnan(F) ? l1 : F;
        float G = F - l2;
        float H = sqrt(G * G + z * z);

        float theta1 = -atan2(y, x) - atan2(F, -l1);
        float theta3 = acos((H * H - l3 * l3 - l4 * l4) / (2 * l3 * l4));
        if (isnan(theta3)) theta3 = 0;
        float theta2 = atan2(z, G) - atan2(l4 * sin(theta3), l3 + l4 * cos(theta3));
        result[0] = theta1 * RAD2DEG;
        result[1] = theta2 * RAD2DEG;
        result[2] = theta3 * RAD2DEG;
    }

    esp_err_t inverse(float a[4][4], float b[4][4]) {
        float s[] = {a[0][0] * a[1][1] - a[1][0] * a[0][1],
                    a[0][0] * a[1][2] - a[1][0] * a[0][2],
                    a[0][0] * a[1][3] - a[1][0] * a[0][3],
                    a[0][1] * a[1][2] - a[1][1] * a[0][2],
                    a[0][1] * a[1][3] - a[1][1] * a[0][3],
                    a[0][2] * a[1][3] - a[1][2] * a[0][3]};
        float c[] = {a[2][0] * a[3][1] - a[3][0] * a[2][1],
                    a[2][0] * a[3][2] - a[3][0] * a[2][2],
                    a[2][0] * a[3][3] - a[3][0] * a[2][3],
                    a[2][1] * a[3][2] - a[3][1] * a[2][2],
                    a[2][1] * a[3][3] - a[3][1] * a[2][3],
                    a[2][2] * a[3][3] - a[3][2] * a[2][3]};
        float det = s[0] * c[5] - s[1] * c[4] + s[2] * c[3] + s[3] * c[2] -
                    s[4] * c[1] + s[5] * c[0];
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