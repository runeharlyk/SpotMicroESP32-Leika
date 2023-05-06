#include "spot_ik.h"
#include "IK_config.h"

#include "esp_dsp.h"
#include "math.h"

// https://github.com/maartenweyn/SpotMicro_ESP32/blob/master/code/esp-idf/ik_test/main/spot_ik.c

static const char* TAG = "IK";

#define RAD2DEGREES 57.295779513082321 // 180 / PI
#define DEGREES2RAD 0.017453292519943

// float orientation[3] = {M_PI / 8, M_PI / 8, 0}; //A 3x1 arry with Spot's Roll, Pitch, Yaw angles - omega, phi, psi
// float position[3] = {0, 0, 0};    //A 3x1 array with Spot's X, Y, Z coordinates
static float Rx[4][4] = {{1, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 1}}; // X-axis rotation matrix
static float Ry[4][4] = {{0, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 1}}; // Y-axis rotation matrix
static float Rz[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}; // Z-axis rotation matrix
static float Rxyz[4][4] = {0,};
static float Tm[4][4] = {0,}; // Transformation Matrix
static float T[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}; //Translation <Atrix
static float Trb[4][4] = {0,}; //Tranformation Matrix Right Back
static float Trf[4][4] = {0,}; //Tranformation Matrix Right Front
static float Tlb[4][4] = {0,}; //Tranformation Matrix Left Back
static float Tlf[4][4] = {0,}; //Tranformation Matrix Left Front

static const float Trb2[4][4] = {{0, 0, 1, -L/2}, {0, 1, 0, 0}, {-1, 0, 0, -W/2}, {0, 0, 0, 1}}; //Fixed part of Tranformation Matrix Right Back
static const float Trf2[4][4] = {{0, 0, 1, L/2},  {0, 1, 0, 0}, {-1, 0, 0, -W/2}, {0, 0, 0, 1}}; //Fixed part of Tranformation Matrix Right Front
static const float Tlb2[4][4] = {{0, 0, 1, -L/2},  {0, 1, 0, 0}, {-1, 0, 0, W/2},  {0, 0, 0, 1}}; //Fixed part of Tranformation Matrix Left Back
static const float Tlf2[4][4] = {{0, 0, 1, L/2}, {0, 1, 0, 0}, {-1, 0, 0, W/2},  {0, 0, 0, 1}}; //Fixed part of Tranformation Matrix Left Front

static float Lp[4][4] = {{80, -130, 100, 1}, {80, -130, -100, 1}, {-130, -130, 100, 1}, {-130, -130, -100, 1}};

static const float Ix[4][4] = {{-1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};

static esp_err_t inverse(float a[4][4], float b[4][4]) {
    float s0 = a[0][0] * a[1][1] - a[1][0] * a[0][1];
    float s1 = a[0][0] * a[1][2] - a[1][0] * a[0][2];
    float s2 = a[0][0] * a[1][3] - a[1][0] * a[0][3];
    float s3 = a[0][1] * a[1][2] - a[1][1] * a[0][2];
    float s4 = a[0][1] * a[1][3] - a[1][1] * a[0][3];
    float s5 = a[0][2] * a[1][3] - a[1][2] * a[0][3];

    float c5 = a[2][2] * a[3][3] - a[3][2] * a[2][3];
    float c4 = a[2][1] * a[3][3] - a[3][1] * a[2][3];
    float c3 = a[2][1] * a[3][2] - a[3][1] * a[2][2];
    float c2 = a[2][0] * a[3][3] - a[3][0] * a[2][3];
    float c1 = a[2][0] * a[3][2] - a[3][0] * a[2][2];
    float c0 = a[2][0] * a[3][1] - a[3][0] * a[2][1];

    // Should check for 0 determinant
    float det = (s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0);

    if (det == 0.0) return ESP_FAIL;

    float invdet = 1.0 / det;

    b[0][0] = ( a[1][1] * c5 - a[1][2] * c4 + a[1][3] * c3) * invdet;
    b[0][1] = (-a[0][1] * c5 + a[0][2] * c4 - a[0][3] * c3) * invdet;
    b[0][2] = ( a[3][1] * s5 - a[3][2] * s4 + a[3][3] * s3) * invdet;
    b[0][3] = (-a[2][1] * s5 + a[2][2] * s4 - a[2][3] * s3) * invdet;

    b[1][0] = (-a[1][0] * c5 + a[1][2] * c2 - a[1][3] * c1) * invdet;
    b[1][1] = ( a[0][0] * c5 - a[0][2] * c2 + a[0][3] * c1) * invdet;
    b[1][2] = (-a[3][0] * s5 + a[3][2] * s2 - a[3][3] * s1) * invdet;
    b[1][3] = ( a[2][0] * s5 - a[2][2] * s2 + a[2][3] * s1) * invdet;

    b[2][0] = ( a[1][0] * c4 - a[1][1] * c2 + a[1][3] * c0) * invdet;
    b[2][1] = (-a[0][0] * c4 + a[0][1] * c2 - a[0][3] * c0) * invdet;
    b[2][2] = ( a[3][0] * s4 - a[3][1] * s2 + a[3][3] * s0) * invdet;
    b[2][3] = (-a[2][0] * s4 + a[2][1] * s2 - a[2][3] * s0) * invdet;

    b[3][0] = (-a[1][0] * c3 + a[1][1] * c1 - a[1][2] * c0) * invdet;
    b[3][1] = ( a[0][0] * c3 - a[0][1] * c1 + a[0][2] * c0) * invdet;
    b[3][2] = (-a[3][0] * s3 + a[3][1] * s1 - a[3][2] * s0) * invdet;
    b[3][3] = ( a[2][0] * s3 - a[2][1] * s1 + a[2][2] * s0) * invdet;

    return ESP_OK;
}

void print_matrix(float * matrix, int n, int m, char* name) {
  printf("Matrix %s:\n", name);
  for (int i=0;i<n;i++) {
    for (int j=0;j<m;j++)
      printf("%.2f ", matrix[i*m + j]);
    printf("\n");
  }
}

void print_int_matrix(int16_t * matrix, int n, int m, char* name, uint8_t newlines) {
  printf("Matrix %s:\n", name);
  for (int i=0;i<n;i++) {
    for (int j=0;j<m;j++)
      printf("%3d ", matrix[i*m + j]);
    if (newlines) printf("\n");
  }
  if (!newlines) printf("\n");
}

esp_err_t body_IK(float omega, float phi, float psi, float xm, float ym, float zm) {
  float cos_omega = cos(omega);
  float sin_omega = sin(omega);
  float cos_phi   = cos(phi);
  float sin_phi   = sin(phi);
  float cos_psi   = cos(psi);
  float sin_psi   = sin(psi);

  Rx[1][1] = cos_omega;
  Rx[1][2] = -sin_omega;
  Rx[2][1] = sin_omega;
  Rx[2][2] = cos_omega;

  Ry[0][0] = cos_phi;
  Ry[0][2] = sin_phi;
  Ry[2][0] = -sin_phi;
  Ry[2][2] = cos_phi;

  Rz[0][0] = cos_psi;
  Rz[0][1] = -sin_psi;
  Rz[1][0] = sin_psi;
  Rz[1][1] = cos_psi;

  float Rxy[4][4];
  dspm_mult_f32_ae32((float*) Rx, (float*) Ry, (float*) Rxy, 4, 4, 4);
  dspm_mult_f32_ae32((float*) Rxy, (float*) Rz, (float*) Rxyz, 4, 4, 4);

  // print_matrix((float*) Rx, 4, 4, "Rx");
  // print_matrix((float*) Ry, 4, 4, "Ry");
  // print_matrix((float*) Rz, 4, 4, "Rz");
  // print_matrix((float*) Rxyz, 4, 4, "Rxyz");

  T[0][3] = xm;
  T[1][3] = ym;
  T[2][3] = zm;

  dsps_add_f32_ae32((float*) T, (float*) Rxyz, (float*) Tm, 16, 1, 1, 1);
  // print_matrix((float*) Tm, 4, 4, "Tm");

  dspm_mult_f32_ae32((float*) Tm, (float*) Trb2, (float*) Trb, 4, 4, 4);
  dspm_mult_f32_ae32((float*) Tm, (float*) Trf2, (float*) Trf, 4, 4, 4);
  dspm_mult_f32_ae32((float*) Tm, (float*) Tlf2, (float*) Tlf, 4, 4, 4);
  dspm_mult_f32_ae32((float*) Tm, (float*) Tlb2, (float*) Tlb, 4, 4, 4);
  // print_matrix((float*) Trb, 4, 4, "Trb");
  // print_matrix((float*) Trf, 4, 4, "Trf");
  // print_matrix((float*) Tlf, 4, 4, "Tlf");
  // print_matrix((float*) Tlb, 4, 4, "Tlb");

  return ESP_OK;
}



esp_err_t leg_IK(float* p, uint8_t leg_id, int16_t* servo_angles) {
    //float x = (leg_id == LEG_RF || leg_id == LEG_RB) ? -p[0] : p[0];
    // Rxy Lenght of shoulder-point on x/y plane only
    float Rxy2 = p[0]*p[0] + p[1]*p[1] - L1L1;
    if (Rxy2 < 0) Rxy2 = 0;
    float Rxy = sqrt(Rxy2);
    // Dxy Distance we need to cover in xy plane
    float Dxy = Rxy - L2;
    // Dxyz 3D Distance we need to cover
    float Dxyz = sqrt(Dxy*Dxy+p[2]*p[2]);

    // ESP_LOGD(tag, "IK Rxy,Dxy,Dxyz %.2f %.2f %.2f", Rxy, Dxy, Dxyz);

    // The angle we need to cover - the angle already covered because of the offset of the servo
    float theta1 = -atan2(p[1], p[0]) - atan2(Rxy, -L1);
    if (isnan(theta1)) return -1;

    // ESP_LOGD(tag, "T1 %.2f = (- %.2f - %.2f)", theta1, -atan2(p[1], p[0]), atan2(Rxy, -L1));

    float d=(Dxyz*Dxyz - L3L3 - L4L4)/(LL34);
    float theta3 = acos(d);
    if (isnan(theta3)) return -3;

    float theta2 = atan2(p[2], Dxy) - atan2(L4 * sin(theta3), L3 + L4 * cos(theta3));
    if (isnan(theta2)) return -2;
    //  ESP_LOGD(tag, "IK %.2f - atan2( %.2f, %.2f) (%.2f)", atan2(p[2], Dxy), L4 * sin(theta3), L3 + L4 * cos(theta3), atan2(L4 * sin(theta3), L3 + L4 * cos(theta3)));


    if (servo_invert[leg_id*3]) {
      servo_angles[0] = 90 - (int16_t) (0.5 + theta1 * RAD2DEGREES);
    } else {
      servo_angles[0] = 90 + (int16_t) (0.5 + theta1 * RAD2DEGREES);
    }

    if (servo_invert[leg_id*3 + 1]) {
      servo_angles[1] = 120 + (int16_t) (0.5 + theta2 * RAD2DEGREES);
    } else {
      servo_angles[1] = 60 - (int16_t) (0.5 + theta2 * RAD2DEGREES);
    }

    if (servo_invert[leg_id*3 + 2]) {
      servo_angles[2] = 180 - (int16_t) (0.5 + theta3 * RAD2DEGREES);
    } else {
      servo_angles[2] = (int16_t) (0.5 + theta3 * RAD2DEGREES);
    }

    if (servo_angles[1] < 0) servo_angles[1] += 360;
    if (servo_angles[1] > 360) servo_angles[1] -= 360;

    // ESP_LOGD(tag, "IK T2 %.2f -> %d", theta2, servo_angles[1]);
    // ESP_LOGD(tag, "IK D %.2f, T3 %.2f -> %d", d, theta3, servo_angles[2]);


    if (theta1 < theta_range[0][0] || theta1 > theta_range[0][1]) return -4;
    if (theta2 < theta_range[1][0] || theta2 > theta_range[1][1]) return -5;
    if (theta3 < theta_range[2][0] || theta3 > theta_range[2][1]) return -6;


    return ESP_OK;
}

esp_err_t spot_IK(float omega, float phi, float psi, float xm, float ym, float zm, int16_t servoangles[4][3]) {
  esp_err_t ret = ESP_OK;
  body_IK(omega, phi, psi, xm, ym, zm);

  float inv[4][4];
  float Q1[4][4];
  float Q[4];


  ret += inverse(Tlf, inv);
  dspm_mult_f32_ae32((float*) inv, (float*) Lp[0], (float*) Q, 4, 4, 1);
  // print_matrix((float*) Q, 1, 4, "Q LF");
  ret += leg_IK((float*) Q, LEG_LF, (int16_t*) servoangles[0]);
  //printf("leg_IK return %d\n", ret);
  //print_int_matrix((int16_t*) servo_angles[0], 1, 3, "servo_angles LF", 1);



  // printf("\n\nRF-----\n");
  ret += inverse(Trf, inv);
  // print_matrix((float*) Ix, 4, 4, "Ix");
  // print_matrix((float*) inv, 4, 4, "inv Trf");
  dspm_mult_f32_ae32((float*) Ix, (float*) inv, (float*) Q1, 4, 4, 4);
  // print_matrix((float*) Q1, 4, 4, "Q1 RF");
  dspm_mult_f32_ae32((float*) Q1, (float*) Lp[1], (float*) Q, 4, 4, 1);
  // print_matrix((float*) Q, 1, 4, "Q RF");
  ret += leg_IK((float*) Q, LEG_RF, (int16_t*) servoangles[1]);
  //printf("leg_IK return %d\n", ret);
  //print_int_matrix((int16_t*) servo_angles[1], 1, 3, "servo_angles RF");

  ret += inverse(Tlb, inv);
  dspm_mult_f32_ae32((float*) inv, (float*) Lp[2], (float*) Q, 4, 4, 1);
  // print_matrix((float*) Q, 1, 4, "Q LB");
  ret += leg_IK((float*) Q, LEG_LB, (int16_t*) servoangles[2]);
  //printf("leg_IK return %d\n", ret);
  //print_int_matrix((int16_t*) servo_angles[2], 1, 3, "servo_angles LB");

  // printf("\n\nRB-----\n");
  ret += inverse(Trb, inv);
  // print_matrix((float*) inv, 4, 4, "inv Trf");
  dspm_mult_f32_ae32((float*) Ix, (float*) inv, (float*) Q1, 4, 4, 4);
  // print_matrix((float*) Q1, 4, 4, "Q1 RF");
  dspm_mult_f32_ae32((float*) Q1, (float*) Lp[3], (float*) Q, 4, 4, 1);

  // print_matrix((float*) Q, 1, 4, "Q RB");
  ret += leg_IK((float*) Q, LEG_RB, (int16_t*) servoangles[3]);
  //printf("leg_IK return %d\n", ret);
  //print_int_matrix((int16_t*) servo_angles[3], 1, 3, "servo_angles RB");

  return ret;
}