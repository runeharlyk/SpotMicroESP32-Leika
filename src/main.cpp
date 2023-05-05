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
#include <camera_pins.h>

DNSServer dnsServer;
AsyncWebSocket ws(WEBSOCKET_PATH);
AsyncEventSource events(EVENTSOURCE_PATH);
AsyncWebServer server(HTTP_PORT);

NewPing sonar[2] = {
  NewPing(USS_LEFT, USS_LEFT, USS_MAX_DISTANCE),
  NewPing(USS_RIGHT, USS_RIGHT, USS_MAX_DISTANCE)
};

MPU6050 mpu(Wire);
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

bool MPU_READY = false;
bool PWM_READY = false;
bool OLED_READY = false;

const bool USE_CAPTIVE_PORTAL = false;

long timer = 0;

bool setupCamera(){
  camera_config_t camera_config;
  camera_config.ledc_channel = LEDC_CHANNEL_0;
  camera_config.ledc_timer = LEDC_TIMER_0;
  camera_config.pin_d0 = Y2_GPIO_NUM;
  camera_config.pin_d1 = Y3_GPIO_NUM;
  camera_config.pin_d2 = Y4_GPIO_NUM;
  camera_config.pin_d3 = Y5_GPIO_NUM;
  camera_config.pin_d4 = Y6_GPIO_NUM;
  camera_config.pin_d5 = Y7_GPIO_NUM;
  camera_config.pin_d6 = Y8_GPIO_NUM;
  camera_config.pin_d7 = Y9_GPIO_NUM;
  camera_config.pin_xclk = XCLK_GPIO_NUM;
  camera_config.pin_pclk = PCLK_GPIO_NUM;
  camera_config.pin_vsync = VSYNC_GPIO_NUM;
  camera_config.pin_href = HREF_GPIO_NUM;
  camera_config.pin_sscb_sda = SIOD_GPIO_NUM;
  camera_config.pin_sscb_scl = SIOC_GPIO_NUM;
  camera_config.pin_pwdn = PWDN_GPIO_NUM;
  camera_config.pin_reset = RESET_GPIO_NUM;
  camera_config.xclk_freq_hz = 20000000;
  camera_config.pixel_format = PIXFORMAT_JPEG; 
  
  if(psramFound()){
      camera_config.frame_size = FRAMESIZE_VGA;
      camera_config.jpeg_quality = 10;
      camera_config.fb_count = 2;
  } else {
      camera_config.frame_size = FRAMESIZE_SVGA;
      camera_config.jpeg_quality = 12;
      camera_config.fb_count = 1;
  }
  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
      printf("Camera probe failed with error 0x%x", err);
      return 0;
  }
  return 1;
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

bool setupPWMController(){
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(50);
  return 1;
}

void setup(){
  Serial.begin(BAUDRATE);
  Serial.setDebugOutput(SERIAL_DEBUG_OUTPUT);

  Wire.begin(SDA, SCL);
  SPIFFS.begin();

  OLED_READY = setupOLED();
  MPU_READY = setupMPU();
  PWM_READY = setupPWMController();

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
