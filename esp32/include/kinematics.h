#ifndef Kinematics_h
#define Kinematics_h

#include <utils/math_utils.h>

/** A variant's leg geometry (m) and the motion limits derived from it. */
struct KinConfig {
    float coxa, coxa_offset, femur, tibia, L, W;
    // Yertle's knee servo turns with its femur, so its angle is the knee's plus the femur's.
    bool kneeFollowsFemur;

    float mountOffsets[4][3];
    float default_feet_positions[4][4];

    float max_roll = 20.0f;
    float max_pitch = 15.0f;

    float max_body_shift_x, max_body_shift_z;
    float max_leg_reach;
    float min_body_height, max_body_height, body_height_range;
    float max_step_length, max_step_height;

    float default_step_depth = 0.002;
    float default_body_height, default_step_height;

    constexpr KinConfig(float coxa, float coxa_offset, float femur, float tibia, float L, float W,
                        bool kneeFollowsFemur)
        : coxa(coxa),
          coxa_offset(coxa_offset),
          femur(femur),
          tibia(tibia),
          L(L),
          W(W),
          kneeFollowsFemur(kneeFollowsFemur),
          mountOffsets {{L / 2, 0, W / 2}, {L / 2, 0, -W / 2}, {-L / 2, 0, W / 2}, {-L / 2, 0, -W / 2}},
          default_feet_positions {{L / 2, 0, W / 2 + coxa, 1},
                                  {L / 2, 0, -W / 2 - coxa, 1},
                                  {-L / 2, 0, W / 2 + coxa, 1},
                                  {-L / 2, 0, -W / 2 - coxa, 1}},
          max_body_shift_x(W / 3),
          max_body_shift_z(W / 3),
          max_leg_reach(femur + tibia - coxa_offset),
          min_body_height(max_leg_reach * 0.45),
          max_body_height(max_leg_reach * 0.9),
          body_height_range(max_body_height - min_body_height),
          max_step_length(max_leg_reach * 0.8),
          max_step_height(max_leg_reach / 2),
          default_body_height(min_body_height + body_height_range / 2),
          default_step_height(default_body_height / 2) {}
};

constexpr KinConfig KIN_CONFIG_SPOTMICRO_ESP32 {0.0605f, 0.010f, 0.1112f, 0.1185f, 0.2075f, 0.078f, false};
constexpr KinConfig KIN_CONFIG_SPOTMICRO_ESP32_MINI {0.035f, 0.0f, 0.060f, 0.060f, 0.160f, 0.080f, false};
constexpr KinConfig KIN_CONFIG_SPOTMICRO_YERTLE {0.035f, 0.0f, 0.130f, 0.130f, 0.240f, 0.078f, true};

struct alignas(16) body_state_t {
    float omega {0}, phi {0}, psi {0}, xm {0}, ym {0}, zm {0};
    float feet[4][4];

    void updateFeet(const float newFeet[4][4]) { COPY_2D_ARRAY_4x4(feet, newFeet); }
};

class Kinematics {
  private:
    const KinConfig &config;

    static constexpr float invMountRot[3][3] = {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}};

    alignas(16) float rot[3][3] = {0};
    alignas(16) float inv_rot[3][3] = {0};
    alignas(16) float inv_trans[3] = {0};


  public:
    explicit Kinematics(const KinConfig &config) : config(config) {}

    esp_err_t calculate_inverse_kinematics(const body_state_t body_state, float result[12]) {
        esp_err_t ret = ESP_OK;

        float roll = body_state.omega * DEG2RAD_F;
        float pitch = body_state.phi * DEG2RAD_F;
        float yaw = body_state.psi * DEG2RAD_F;
        euler2R(roll, pitch, yaw, rot);
        inverse(rot, inv_rot);

        inv_trans[0] =
            -inv_rot[0][0] * body_state.xm - inv_rot[0][1] * body_state.ym - inv_rot[0][2] * body_state.zm;
        inv_trans[1] =
            -inv_rot[1][0] * body_state.xm - inv_rot[1][1] * body_state.ym - inv_rot[1][2] * body_state.zm;
        inv_trans[2] =
            -inv_rot[2][0] * body_state.xm - inv_rot[2][1] * body_state.ym - inv_rot[2][2] * body_state.zm;

        for (int i = 0; i < 4; i++) {
            float wx = body_state.feet[i][0];
            float wy = body_state.feet[i][1];
            float wz = body_state.feet[i][2];

            float bx = inv_rot[0][0] * wx + inv_rot[0][1] * wy + inv_rot[0][2] * wz + inv_trans[0];
            float by = inv_rot[1][0] * wx + inv_rot[1][1] * wy + inv_rot[1][2] * wz + inv_trans[1];
            float bz = inv_rot[2][0] * wx + inv_rot[2][1] * wy + inv_rot[2][2] * wz + inv_trans[2];

            float mx = config.mountOffsets[i][0];
            float my = config.mountOffsets[i][1];
            float mz = config.mountOffsets[i][2];

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

    inline void legIK(float x, float y, float z, float out[3]) {
        const float coxa = config.coxa, coxa_offset = config.coxa_offset, femur = config.femur, tibia = config.tibia;
        float F = sqrt(fmax(0.0f, x * x + y * y - coxa * coxa));
        float G = F - coxa_offset;
        float H = sqrt(G * G + z * z);

        float theta1 = -atan2f(y, x) - atan2f(F, -coxa);
        float D = (H * H - femur * femur - tibia * tibia) / (2 * femur * tibia);
        float theta3 = acosf(fmax(-1.0f, fmin(1.0f, D)));
        float theta2 = atan2f(z, G) - atan2f(tibia * sinf(theta3), femur + tibia * cosf(theta3));
        out[0] = RAD_TO_DEG_F(theta1);
        out[1] = RAD_TO_DEG_F(theta2);
        out[2] = RAD_TO_DEG_F(config.kneeFollowsFemur ? theta3 + theta2 : theta3);
    }
};

#endif