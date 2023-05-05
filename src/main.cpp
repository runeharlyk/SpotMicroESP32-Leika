#include <Arduino.h>
#include "OV2640.h"
#include <WiFi.h>

#include <index_other.h>

//#define USE_SONAR
//#define USE_MPU
//#define USE_OLED
//#define USE_PWM
#define USE_WEBSOCKET
//#define USE_BUTTON

#define FILESYSTEM SPIFFS
#if FILESYSTEM == FFat
#include <FFat.h>
#endif
#if FILESYSTEM == SPIFFS
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#endif

#ifdef USE_SONAR
#include <NewPing.h>
#endif
//#include <SPI.h>
#include "Wire.h"
#ifdef USE_OLED
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif
#ifdef USE_MPU
#include <MPU6050_light.h>
#endif
#ifdef USE_PWM
#include <Adafruit_PWMServoDriver.h>
#endif

#include <AsyncJpegStreamHandler.h>
#include <CaptivePortalHandler.h>
#include <WebsocketHandler.h>

#define CAMERA_MODEL_AI_THINKER
#include <camera_pins.h>

const char* ssid = "";
const char* password = "";
DNSServer dnsServer;
AsyncWebSocket ws(WEBSOCKET_PATH);
AsyncEventSource events(EVENTSOURCE_PATH);
AsyncWebServer server(HTTP_PORT);

#ifdef USE_BUTTON
int buttonLed = 2;
int button = 16;
bool servosEnabled = true;
int currentButtonState = 0;
#endif

#ifdef USE_SONAR
NewPing sonar[2] = {
  NewPing(12, 12, 200),
  NewPing(13, 13, 200)
};
#endif

#ifdef USE_MPU
  MPU6050 mpu(Wire);
  bool MPU_READY = false;
#endif

#ifdef USE_PWM
  Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);
  bool PWM_READY = false;
  
#endif
#ifdef USE_OLED
  #define SCREEN_WIDTH 128
  #define SCREEN_HEIGHT 64
  Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
  bool OLED_READY = false;
#endif

OV2640 cam;


long timer = 0;

  } else {
  }
  }
}

  }

}

    }
  }
}

}

#ifdef USE_MPU
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
#endif
#ifdef USE_OLED
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
#endif
#ifdef USE_PWM
bool setupPWMController(){
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(50);
  return 1;
}
#endif
#ifdef USE_BUTTON
bool debounce() {
  static uint16_t state = 0;
  state = (state<<1) | digitalRead(button) | 0xfe00;
  return (state == 0xff00);
}
#endif
void setup() {
  //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  Wire.begin(14, 15);

  #ifdef USE_BUTTON
  pinMode(buttonLed, OUTPUT);
  pinMode(button, INPUT);
  #endif
  
  #ifdef USE_MPU
  MPU_READY = setupMPU();
  #endif
  #ifdef USE_PWM
  PWM_READY = setupPWMController();
  #endif
  #ifdef USE_OLED
  OLED_READY = setupOLED();
  #endif
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; 
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Camera init
  cam.init(config);
  // Wi-Fi connection
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  
  Serial.print("Camera Stream Ready! Go to: http://");
  Serial.print(WiFi.localIP());
  #ifdef USE_OLED
  if(OLED_READY){
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0,0);
    display.println(WiFi.localIP());
    display.display();
  }
  #endif
  

}



  if(millis() - timer > 500) {
    timer = millis();
  }

  #ifdef USE_BUTTON
  if (debounce()) {
    servosEnabled = !servosEnabled;
    digitalWrite(buttonLed, servosEnabled);
  }
  #endif
}

