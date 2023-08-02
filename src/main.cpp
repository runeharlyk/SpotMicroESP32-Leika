#include <globals.h>
#include <deviceconfig.h>


ESPTaskManager g_TaskManager;
#if USE_WIFI && USE_WEBSERVER
    DRAM_ATTR CWebServer g_WebServer;
#endif

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(BAUDRATE);
    log_i("Booting");
    SPIFFS.begin();
    Wire.begin(SDA, SCL);
    InitializeCamera();
    g_TaskManager.begin();
    g_TaskManager.StartThreads();

    g_ptrJSONWriter = std::make_unique<JSONWriter>();
    g_ptrDeviceConfig = std::make_unique<DeviceConfig>();
    g_ptrServo = std::make_unique<Servo>();

    g_ptrServo->begin();
    g_ptrServo->setOscillatorFrequency(SERVO_OSCILLATOR_FREQUENCY);
    g_ptrServo->setPWMFreq(SERVO_FREQ);
    g_ptrServo->updateServos();
}

void loop() {
    delay(200);
}
