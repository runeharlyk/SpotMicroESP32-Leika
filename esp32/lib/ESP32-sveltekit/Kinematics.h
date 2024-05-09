#ifndef Kinematics_h
#define Kinematics_h

#include <cmath>
#include <esp_dsp.h>

typedef  struct {
    float omega;
    float phi;
    float psi;
    float xm;
    float ym;
    float zm;
    bool set;
} position_t;


#define RAD2DEGREES 57.295779513082321 // 180 / PI
#define DEGREES2RAD 0.017453292519943

class Kinematics
{
private:
    dspm::Mat Tlf;
    dspm::Mat Trf;
    dspm::Mat Tlb;
    dspm::Mat Trb;

    float inverse[4][4];

    dspm::Mat Ix;

public:
    float l1, l2, l3, l4;
    float L, W;
    Kinematics(){
        l1 = 50;
        l2 = 20;
        l3 = 120;
        l4 = 155;

        L = 140;
        W = 75;

        float Ix_data[] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        dspm::Mat Ix(Ix_data, 4, 4);
    }
    ~Kinematics(){}

    esp_err_t calculate_inverse_kinematics(float lp[4][4], position_t p, float result[12]) {
        
        esp_err_t res = bodyIK(p);

        for (int i = 0; i < 4; ++i) {
            dspm::Mat temp(lp[i], 4, 1);
            dspm::Mat result_vec(4, 1);

            if (i == 1 || i == 3) {
                result_vec = Ix * ((i == 1 ? Trf.inverse() : Trb.inverse()) * temp);
            } else {
                result_vec = (i == 0 ? Tlf.inverse() : Tlb.inverse()) * temp;
            }

            legIK(result_vec.data, &result[i * 3]);
        }

        return res;
    }

    esp_err_t bodyIK(position_t p) {
        float cos_omega = cos(p.omega*DEGREES2RAD);
        float sin_omega = sin(p.omega*DEGREES2RAD);
        float cos_phi = cos(p.phi*DEGREES2RAD);
        float sin_phi = sin(p.phi*DEGREES2RAD);
        float cos_psi = cos(p.psi*DEGREES2RAD);
        float sin_psi = sin(p.psi*DEGREES2RAD);

        float Rx_data[] = {
            1, 0, 0, 0,
            0, cos_omega, -sin_omega, 0,
            0, sin_omega, cos_omega, 0,
            0, 0, 0, 1
        };
        dspm::Mat Rx(Rx_data, 4, 4);

        float Ry_data[] = {
            cos_phi, 0, sin_phi, 0,
            0, 1, 0, 0,
            -sin_phi, 0, cos_phi, 0,
            0, 0, 0, 1
        };
        dspm::Mat Ry(Ry_data, 4, 4);

        float Rz_data[] = {
            cos_psi, -sin_psi, 0, 0,
            sin_psi, cos_psi, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        dspm::Mat Rz(Rz_data, 4, 4);

        dspm::Mat Rxyz = Rx * Ry * Rz;

        float T_data[] = {
            0, 0, 0, p.xm,
            0, 0, 0, p.ym,
            0, 0, 0, p.zm,
            0, 0, 0, 0
        };
        dspm::Mat T(T_data, 4, 4);

        dspm::Mat Tm = T + Rxyz;

        float sHp = sin(M_PI / 2);
        float cHp = cos(M_PI / 2);

        float points_lf[] = {
            cHp, 0, sHp, L / 2,
            0, 1, 0, 0,
            -sHp, 0, cHp, W / 2,
            0, 0, 0, 1
        };
        Tlf = Tm * dspm::Mat(points_lf, 4, 4);

        float points_rf[] = {
            cHp, 0, sHp, L / 2,
            0, 1, 0, 0,
            -sHp, 0, cHp, -W / 2,
            0, 0, 0, 1
        };
        Trf = Tm * dspm::Mat(points_rf, 4, 4);

        float points_lb[] = {
            cHp, 0, sHp, -L / 2,
            0, 1, 0, 0,
            -sHp, 0, cHp, W / 2,
            0, 0, 0, 1
        };
        Tlb = Tm * dspm::Mat(points_lb, 4, 4);

        float points_rb[] = {
            cHp, 0, sHp, -L / 2,
            0, 1, 0, 0,
            -sHp, 0, cHp, -W / 2,
            0, 0, 0, 1
        };
        Trb = Tm * dspm::Mat(points_rb, 4, 4);
        return ESP_OK;
    }

    void legIK(float point[4], float result[3]) {
        float x = point[0];
        float y = point[1];
        float z = point[2];

        float F = sqrt(x * x + y * y - l1 * l1);
        if (isnan(F)) F = l1;
        float G = F - l2;
        float H = sqrt(G * G + z * z);

        result[0] = -atan2(y, x) - atan2(F, -l1);
        result[2] = acos((H * H - l3 * l3 - l4 * l4) / (2 * l3 * l4));
        if (isnan(result[2])) result[2] = 0;
        result[1] = atan2(z, G) - atan2(l4 * sin(result[2]), l3 + l4 * cos(result[2]));
        result[0] *= RAD2DEGREES;
        result[1] *= RAD2DEGREES;
        result[2] *= RAD2DEGREES;
    }
};

#endif