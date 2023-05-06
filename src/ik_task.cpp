#include <ik_task.h>

static const char* TAG = "IK TASK";

position_t spot_position = {.omega=0,.phi=0,.psi=0,.xm=-40,.ym=-170, .zm=0, .set=1};
position_t goal_position = {0,};
int16_t servo_angles[4][3] = {{90, 150, 0}, {90, 30, 180}, {90, 150, 0}, {90, 30, 180}};
int16_t servo_angles_goal[4][3] = {0,};

void set_orientation_cb(int16_t omega, int16_t phi, int16_t psi, int16_t xm, int16_t ym, int16_t zm) {
  goal_position.omega = omega;
  goal_position.phi = phi;
  goal_position.psi = psi;
  goal_position.xm  = xm;
  goal_position.ym = ym;
  goal_position.zm = zm;
  goal_position.set = true;
}

void reset_position() {
    set_orientation_cb(0, 0, 0, 0, 0, 0);
}

void set_leg_servos() {
    for (int l = 0; l<4; l++) {
    // for (int l = 1; l<2; l++) {    
        for (int s=0;s<3;s++) {
            if (servo_angles[l][s] != servo_angles_goal[l][s]) {
                servo_angles[l][s] = servo_angles_goal[l][s];
                set_servo(l*3 + s, servo_angles[l][s]);
            }
        }
    }
}

void iterate_to_position() {
    ESP_LOGI(TAG, "GOAL (%f,%f,%f - %f,%f,%f)", goal_position.omega, goal_position.phi, goal_position.psi, goal_position.xm, goal_position.ym, goal_position.zm);

    do {
    spot_position.set = false;
    int diff = 0;

    if (goal_position.omega != spot_position.omega) {
        diff = goal_position.omega - spot_position.omega;
        if (abs(diff) < MOTION_STEP_ANGLE) {
            spot_position.omega = goal_position.omega ;
        } else {
            diff = diff < 0 ?  -MOTION_STEP_ANGLE : MOTION_STEP_ANGLE;
            spot_position.omega += diff;
            spot_position.set = true;
        }
    }

    if (goal_position.phi != spot_position.phi) {
        diff = goal_position.phi - spot_position.phi;
        if (abs(diff) < MOTION_STEP_ANGLE) {
            spot_position.phi = goal_position.phi ;
        } else {
            diff = diff < 0 ?  -MOTION_STEP_ANGLE : MOTION_STEP_ANGLE;
            spot_position.phi += diff;
            spot_position.set = true;
        }
    }

    if (goal_position.psi != spot_position.psi) {
        diff = goal_position.psi - spot_position.psi;
        if (abs(diff) < MOTION_STEP_ANGLE) {
            spot_position.psi = goal_position.psi ;
        } else {
            diff = diff < 0 ?  -MOTION_STEP_ANGLE : MOTION_STEP_ANGLE;
            spot_position.psi += diff;
            spot_position.set = true;
        }
    }

    if (goal_position.xm != spot_position.xm) {
        diff = goal_position.xm - spot_position.xm;
        if (abs(diff) < MOTION_STEP_MOVEMENT) {
            spot_position.xm = goal_position.xm ;
        } else {
            diff = diff < 0 ?  -MOTION_STEP_MOVEMENT : MOTION_STEP_MOVEMENT;
            spot_position.xm += diff;
            spot_position.set = true;
        }
    }

    if (goal_position.ym != spot_position.ym) {
        diff = goal_position.ym - spot_position.ym;
        if (abs(diff) < MOTION_STEP_MOVEMENT) {
            spot_position.ym = goal_position.ym ;
        } else {
            diff = diff < 0 ?  -MOTION_STEP_MOVEMENT : MOTION_STEP_MOVEMENT;
            spot_position.ym += diff;
            spot_position.set = true;
        }
    }

    if (goal_position.zm != spot_position.zm) {
        diff = goal_position.zm - spot_position.zm;
        if (abs(diff) < MOTION_STEP_MOVEMENT) {
            spot_position.zm = goal_position.zm ;
        } else {
            diff = diff < 0 ?  -MOTION_STEP_MOVEMENT : MOTION_STEP_MOVEMENT;
            spot_position.zm += diff;
            spot_position.set = true;
        }
    }


    ESP_LOGI(TAG, "CURRENT (%f,%f,%f - %f,%f,%f) %d", spot_position.omega, spot_position.phi, spot_position.psi, spot_position.xm, spot_position.ym, spot_position.zm, spot_position.set);
    
    esp_err_t ret = spot_IK(spot_position.omega*DEGREES2RAD, spot_position.phi*DEGREES2RAD, spot_position.psi*DEGREES2RAD, spot_position.xm, spot_position.ym, spot_position.zm, servo_angles_goal);
    ESP_LOGD(TAG, "Valid IK %d", ret==ESP_OK);
    if (ret == ESP_OK) {
        //print_int_matrix((int16_t*) servo_angles_goal, 4, 3, "servo_angles_goal", false);
        set_leg_servos();
    }

    } while (spot_position.set);


    //set_new_orientation_act_value((int16_t) spot_position.omega, (int16_t) spot_position.phi, (int16_t) spot_position.psi, (int16_t) spot_position.xm, (int16_t) spot_position.ym, (int16_t) spot_position.zm);
}

void task_ik(void *ignore){
    ESP_LOGI(TAG, "Executing on core %d", xPortGetCoreID());
    esp_err_t ret;

    reset_position();
    
    for(;;) {
        vTaskDelay(100 / portTICK_RATE_MS);
        if (goal_position.set) {
            goal_position.set = false;
            iterate_to_position();
        }
    }
    vTaskDelete(NULL);
}
