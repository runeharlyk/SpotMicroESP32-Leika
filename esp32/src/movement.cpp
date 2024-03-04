#include <featureflags.h>
#if USE_MPU
    #include <MPU6050_light.h>
    #include <Adafruit_Sensor.h>
    #include <Adafruit_HMC5883_U.h>
#endif

#if USE_POWER_BUTTON
    #include <Bounce2.h>
#endif

#if USE_POWER_BUTTON
    Bounce2::Button PowerButton;
#endif

#if USE_MPU
    DRAM_ATTR MPU6050 g_imu(Wire);
    DRAM_ATTR Adafruit_HMC5883_Unified g_mag = Adafruit_HMC5883_Unified(12345);
#endif

void beginMag() {
    if(!g_mag.begin()) log_w("Failed to initialize HMC5883L");
}

void beginImu() {
    byte status = g_imu.begin();
    if(status != 0) log_w("MPU initialize failed");
    delay(100);
    g_imu.calcOffsets(true,true);
}

float getHeading() {
    sensors_event_t event;
    g_mag.getEvent(&event);

    float heading = atan2(event.magnetic.y, event.magnetic.x);
    float declinationAngle = 0.22;
    heading += declinationAngle;
    if(heading < 0) heading += 2*PI;
    if(heading > 2*PI) heading -= 2*PI;
    return heading * 180/M_PI;
}

float getTemp() {
    return g_imu.getTemp();
}

float getAngleX() {
    return g_imu.getAngleX();
}

float getAngleY() {
    return g_imu.getAngleX();
}

float getAngleZ() {
    return g_imu.getAngleZ();
}

void IRAM_ATTR MovementHandlingLoopEntry(void *) {
    TickType_t notifyWait = 0;
    beginMag();
    beginImu();

    for (;;) {
        g_imu.update();
        ulTaskNotifyTake(pdTRUE, notifyWait);

        // #if USE_POWER_BUTTON
        //     PowerButton.update();
        //     if (PowerButton.pressed()) {
        //         log_i("Power Button Pressed");
        //         g_ptrServo->toggleState();
        //     }
        // #endif

        notifyWait = pdMS_TO_TICKS(100);
    }
}