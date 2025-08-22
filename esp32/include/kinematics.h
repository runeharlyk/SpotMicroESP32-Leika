#ifndef Kinematics_h
#define Kinematics_h

#include <utils/math_utils.h>

struct alignas(16) body_state_t {
    float omega, phi, psi, xm, ym, zm;
    float feet[4][4];

    void updateFeet(const float newFeet[4][4]) { COPY_2D_ARRAY_4x4(feet, newFeet); }

    bool operator==(const body_state_t &other) const {
        if (!IS_ALMOST_EQUAL(omega, other.omega) || !IS_ALMOST_EQUAL(phi, other.phi) ||
            !IS_ALMOST_EQUAL(psi, other.psi) || !IS_ALMOST_EQUAL(xm, other.xm) || !IS_ALMOST_EQUAL(ym, other.ym) ||
            !IS_ALMOST_EQUAL(zm, other.zm)) {
            return false;
        }
        return arrayEqual(feet, other.feet, 0.1f);
    }
};

class Kinematics {
  private:
#if defined(SPOTMICRO_ESP32)
    static constexpr float l1 = 60.5f / 100.0f;
    static constexpr float l2 = 10.0f / 100.0f;
    static constexpr float l3 = 111.2f / 100.0f;
    static constexpr float l4 = 118.5f / 100.0f;

    static constexpr float L = 207.5f / 100.0f;
    static constexpr float W = 78.0f / 100.0f;
#elif defined(SPOTMICRO_ESP32_MINI)
    static constexpr float l1 = 0.0f / 100.0f;
    static constexpr float l2 = 0.0f / 100.0f;
    static constexpr float l3 = 52.0f / 100.0f;
    static constexpr float l4 = 65.0f / 100.0f;

    static constexpr float L = 120.0f / 100.0f;
    static constexpr float W = 78.5f / 100.0f;
#elif defined(SPOTMICRO_YERTLE)
    static constexpr float l1 = 35.0f / 100.0f;
    static constexpr float l2 = 0.0f;
    static constexpr float l3 = 130.0f / 100.0f;
    static constexpr float l4 = 130.0f / 100.0f;

    static constexpr float L = 240.0f / 100.0f;
    static constexpr float W = 78.0f / 100.0f;
#else
#error "Must define either SPOTMICRO_ESP32 or SPOTMICRO_YERTLE"
#endif

    static constexpr float mountOffsets[4][3] = {
        {L / 2, 0, W / 2}, {L / 2, 0, -W / 2}, {-L / 2, 0, W / 2}, {-L / 2, 0, -W / 2}};

    static constexpr float invMountRot[3][3] = {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}};

    alignas(16) float rot[3][3] = {0};
    alignas(16) float inv_rot[3][3] = {0};
    alignas(16) float inv_trans[3] = {0};

    body_state_t currentState;

  public:
    static constexpr float default_feet_positions[4][4] = {
        {mountOffsets[0][0], -1, mountOffsets[0][2] + l1, 1},
        {mountOffsets[1][0], -1, mountOffsets[1][2] - l1, 1},
        {mountOffsets[2][0], -1, mountOffsets[2][2] + l1, 1},
        {mountOffsets[3][0], -1, mountOffsets[3][2] - l1, 1},
    };

    esp_err_t calculate_inverse_kinematics(const body_state_t body_state, float result[12]) {
        esp_err_t ret = ESP_OK;

        if (currentState == body_state) return ESP_OK;

        currentState.omega = body_state.omega;
        currentState.phi = body_state.phi;
        currentState.psi = body_state.psi;
        currentState.xm = body_state.xm;
        currentState.ym = body_state.ym;
        currentState.zm = body_state.zm;
        currentState.updateFeet(body_state.feet);

        float roll = body_state.omega * DEG2RAD_F;
        float pitch = body_state.phi * DEG2RAD_F;
        float yaw = body_state.psi * DEG2RAD_F;
        euler2R(roll, pitch, yaw, rot);
        inverse(rot, inv_rot);

        inv_trans[0] =
            -inv_rot[0][0] * currentState.xm - inv_rot[0][1] * currentState.ym - inv_rot[0][2] * currentState.zm;
        inv_trans[1] =
            -inv_rot[1][0] * currentState.xm - inv_rot[1][1] * currentState.ym - inv_rot[1][2] * currentState.zm;
        inv_trans[2] =
            -inv_rot[2][0] * currentState.xm - inv_rot[2][1] * currentState.ym - inv_rot[2][2] * currentState.zm;

        for (int i = 0; i < 4; i++) {
            float wx = currentState.feet[i][0];
            float wy = currentState.feet[i][1];
            float wz = currentState.feet[i][2];

            float bx = inv_rot[0][0] * wx + inv_rot[0][1] * wy + inv_rot[0][2] * wz + inv_trans[0];
            float by = inv_rot[1][0] * wx + inv_rot[1][1] * wy + inv_rot[1][2] * wz + inv_trans[1];
            float bz = inv_rot[2][0] * wx + inv_rot[2][1] * wy + inv_rot[2][2] * wz + inv_trans[2];

            float mx = mountOffsets[i][0];
            float my = mountOffsets[i][1];
            float mz = mountOffsets[i][2];

            float px = bx - mx;
            float py = by - my;
            float pz = bz - mz;

            float lx = invMountRot[0][0] * px + invMountRot[0][1] * py + invMountRot[0][2] * pz;
            float ly = invMountRot[1][0] * px + invMountRot[1][1] * py + invMountRot[1][2] * pz;
            float lz = invMountRot[2][0] * px + invMountRot[2][1] * py + invMountRot[2][2] * pz;

            float xLocal = (i % 2 == 1) ? -lx : lx;
            legIK(xLocal, ly, lz, result + i * 3);
        }

        return ret;
    }

    inline void euler2R(float roll, float pitch, float yaw, float rot[3][3]) {
        float cos_roll = std::cos(roll);
        float sin_roll = std::sin(roll);
        float cos_pitch = std::cos(pitch);
        float sin_pitch = std::sin(pitch);
        float cos_yaw = std::cos(yaw);
        float sin_yaw = std::sin(yaw);

        rot[0][0] = cos_pitch * cos_yaw;
        rot[0][1] = -sin_yaw * cos_pitch;
        rot[0][2] = sin_pitch;
        rot[1][0] = sin_roll * sin_pitch * cos_yaw + sin_yaw * cos_roll;
        rot[1][1] = -sin_roll * sin_pitch * sin_yaw + cos_roll * cos_yaw;
        rot[1][2] = -sin_roll * cos_pitch;
        rot[2][0] = sin_roll * sin_yaw - sin_pitch * cos_roll * cos_yaw;
        rot[2][1] = sin_roll * cos_yaw + sin_pitch * sin_yaw * cos_roll;
        rot[2][2] = cos_roll * cos_pitch;
    }

    inline void inverse(float rot[3][3], float inv_rot[3][3]) {
        inv_rot[0][0] = rot[0][0];
        inv_rot[0][1] = rot[1][0];
        inv_rot[0][2] = rot[2][0];
        inv_rot[1][0] = rot[0][1];
        inv_rot[1][1] = rot[1][1];
        inv_rot[1][2] = rot[2][1];
        inv_rot[2][0] = rot[0][2];
        inv_rot[2][1] = rot[1][2];
        inv_rot[2][2] = rot[2][2];
    }

    inline void legIK(float x, float y, float z, float result[3]) {
        float F = sqrt(max(0.0f, x * x + y * y - l1 * l1));
        float G = F - l2;
        float H = sqrt(G * G + z * z);

        float theta1 = -atan2f(y, x) - atan2f(F, -l1);
        float D = (H * H - l3 * l3 - l4 * l4) / (2 * l3 * l4);
        float theta3 = acosf(max(-1.0f, min(1.0f, D)));
        float theta2 = atan2f(z, G) - atan2f(l4 * sinf(theta3), l3 + l4 * cosf(theta3));
        result[0] = RAD_TO_DEG_F(theta1);
        result[1] = RAD_TO_DEG_F(theta2);
#if defined(SPOTMICRO_ESP32) || defined(SPOTMICRO_ESP32_MINI)
        result[2] = RAD_TO_DEG_F(theta3);
#elif defined(SPOTMICRO_YERTLE)
        result[2] = RAD_TO_DEG_F(theta3 + theta2);
#endif
    }
};

#endif