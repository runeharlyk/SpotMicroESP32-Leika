#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <ArduinoOTA.h>
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems

#include <SPI.h>
#include <Wire.h>
#include <NewPing.h>
#include <MPU6050_light.h>
#include <Adafruit_PWMServoDriver.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

#include <AsyncJpegStreamHandler.h>
#include <WebsocketHandler.h>

#include <config.h>
#include <camera.h>

#include <servo.h>
#include <ik_task.h>

static const char* TAG = "MAIN";

DNSServer dnsServer;
AsyncWebSocket ws(WEBSOCKET_PATH);
AsyncEventSource events(EVENTSOURCE_PATH);
AsyncWebServer server(HTTP_PORT);

NewPing sonar[2] = {
  NewPing(USS_LEFT, USS_LEFT, USS_MAX_DISTANCE),
  NewPing(USS_RIGHT, USS_RIGHT, USS_MAX_DISTANCE)
};

MPU6050 mpu(Wire);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SCREEN_RESET);

bool MPU_READY = false;
bool PWM_READY = false;
bool OLED_READY = false;

const bool USE_CAPTIVE_PORTAL = false;

int lastState = LOW;
int currentState;

long timer = 0;

#ifdef __cplusplus
  extern "C" {
    #endif
    uint8_t temprature_sens_read();
    #ifdef __cplusplus
  }
#endif
uint8_t temprature_sens_read();

bool setupOLED(){
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    return 0;
  }

  display.display();
  delay(200);
  display.clearDisplay();
  return 1;
}

void setupWiFi(){
  if(USE_CAPTIVE_PORTAL){
    WiFi.softAP(HOSTNAME);
    dnsServer.start(53, "*", WiFi.softAPIP());
  } else {
    WiFi.begin(SSID, PASS);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }
    if(OLED_READY) {
      display.setTextColor(WHITE, BLACK);
      display.setTextSize(1);
      int16_t x1 = 0;
      int16_t y1 = 0;
      uint16_t h = 0;
      uint16_t w = 0;
      display.getTextBounds(WiFi.localIP().toString(), 0, 0, &x1, &y1, &w, &h);
      display.setCursor(SCREEN_WIDTH/2 - w/2, SCREEN_HEIGHT/2 - h/2);
      display.println(WiFi.localIP());
      display.display();
    }
  }
  if(MDNS.begin(HOSTNAME)){
    MDNS.addService("http", "tcp", HTTP_PORT);
  }
}

void setupServer(){
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/stream", HTTP_GET, streamJpg);
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  server.begin();
}

bool setupMPU(){
  byte status = mpu.begin();
  Serial.print(F("MPU6050 status: "));
  Serial.println(status);
  if(status != 0){ return 0; }
  Serial.println(F("Calculating offsets, do not move MPU6050"));
  delay(1000);
  mpu.calcOffsets(true,true);
  Serial.println("Done!\n");
  return 1;
}

void setupOAT(){
  ArduinoOTA.onStart([]() { events.send("Update Start", "ota"); });
  ArduinoOTA.onEnd([]() { events.send("Update End", "ota"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char p[32];
    sprintf(p, "Progress: %u%%\n", (progress/(total/100)));
    events.send(p, "ota");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if(error == OTA_AUTH_ERROR) events.send("Auth Failed", "ota");
    else if(error == OTA_BEGIN_ERROR) events.send("Begin Failed", "ota");
    else if(error == OTA_CONNECT_ERROR) events.send("Connect Failed", "ota");
    else if(error == OTA_RECEIVE_ERROR) events.send("Recieve Failed", "ota");
    else if(error == OTA_END_ERROR) events.send("End Failed", "ota");
  });
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.begin();
}

void setup(){
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(BAUDRATE);
  Serial.setDebugOutput(SERIAL_DEBUG_OUTPUT);

  pinMode(BUTTON_LED, OUTPUT);
  //pinMode(BUTTON, INPUT);

  Wire.begin(SDA, SCL);
  SPIFFS.begin();

  OLED_READY = setupOLED();
  MPU_READY = setupMPU();
  PWM_READY = setup_pwm_controller();

  setupCamera();
  setupWiFi();
  setupOAT();
  setupServer();

  digitalWrite(BUTTON_LED, HIGH);
}

void loop(){
  ArduinoOTA.handle();
  if(USE_CAPTIVE_PORTAL) dnsServer.processNextRequest();
  ws.cleanupClients();

  currentState = digitalRead(BUTTON);

  if (lastState == HIGH && currentState == LOW)
    disable_servos();
  lastState = currentState;

  if(millis() - timer > 50) {

    /*int sketchSize = ESP.getSketchSize();
    int sketchSpace = ESP.getFreeSketchSpace();
    const char* SdkVersion = ESP.getSdkVersion();
    uint32_t mhz = ESP.getCpuFreqMHz();
    uint32_t heapSize = ESP.getHeapSize();
    uint32_t psramSize = ESP.getPsramSize();

    size_t filesystem_used = SPIFFS.usedBytes();
    size_t filesystem_total = SPIFFS.totalBytes();

    int64_t sec = esp_timer_get_time() / 1000000;
    int64_t upDays = int64_t(floor(sec/86400));
    int upHours = int64_t(floor(sec/3600)) % 24;
    int upMin = int64_t(floor(sec/60)) % 60;
    int upSec = sec % 60;*/

    mpu.update();
    size_t numContent = 13;
    float content[numContent];
    content[0] = WiFi.RSSI();
    content[1] = mpu.getTemp();
    content[2] = mpu.getAccX();
    content[3] = mpu.getAccY();
    content[4] = mpu.getAccZ();
    content[5] = mpu.getAngleX();
    content[6] = mpu.getAngleY();
    content[7] = mpu.getAngleZ();
    content[8] = (temprature_sens_read() - 32) / 1.8;
    content[9] = sonar[0].ping_cm();
    content[10] = sonar[1].ping_cm();
    content[11] = ESP.getFreeHeap();
    content[12] = ESP.getFreePsram();

    uint8_t* buf = (uint8_t*) &content;
    size_t buf_len = sizeof(buf);

    ws.binaryAll(buf, buf_len * numContent);
    timer = millis();
  }
}
