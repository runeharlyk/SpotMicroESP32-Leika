#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <esp_camera.h>

#include <Wire.h>
#include <NewPing.h>
#include <MPU6050_light.h>
#include <Adafruit_PWMServoDriver.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

#include <AsyncJpegStreamHandler.h>
#include <CaptivePortalHandler.h>
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

long timer = 0;

  }

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
      Serial.print(".");
    }
    Serial.println("");
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    if(OLED_READY){
      display.setTextSize(2);
      display.setCursor(0,0);
      display.println(WiFi.localIP());
      display.display();
    }
  }
  MDNS.addService("http", "tcp", HTTP_PORT);
  MDNS.begin(HOSTNAME);
}

void setupServer(){
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/stream", HTTP_GET, streamJpg);
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
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

}

void setup(){
  Serial.begin(BAUDRATE);
  Serial.setDebugOutput(SERIAL_DEBUG_OUTPUT);

  Wire.begin(SDA, SCL);
  SPIFFS.begin();

  OLED_READY = setupOLED();
  MPU_READY = setupMPU();
  PWM_READY = setup_pwm_controller();

  setupCamera();
  setupWiFi();
  setupServer();
}

void loop(){
  if(USE_CAPTIVE_PORTAL) dnsServer.processNextRequest();
  ws.cleanupClients();

  if(millis() - timer > 500) {
    String message = String(WiFi.RSSI());
    ws.textAll(message);
    timer = millis();
  }
}
