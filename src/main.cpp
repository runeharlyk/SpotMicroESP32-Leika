#include <Arduino.h>
#include "OV2640.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>

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
#ifdef USE_WEBSOCKET
#include <WebSocketsServer.h>
#endif


#define CAMERA_MODEL_AI_THINKER
#include <camera_pins.h>

const char* ssid = "";
const char* password = "";

const char HEADER[] = "HTTP/1.1 200 OK\r\n" \
                      "Access-Control-Allow-Origin: *\r\n" \
                      "Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321\r\n";
const char BOUNDARY[] = "\r\n--123456789000000000000987654321\r\n";
const char CTNTTYPE[] = "Content-Type: image/jpeg\r\nContent-Length: ";
const int hdrLen = strlen(HEADER);
const int bdrLen = strlen(BOUNDARY);
const int cntLen = strlen(CTNTTYPE);

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

#ifdef USE_WEBSOCKET
WebSocketsServer webSocket = WebSocketsServer(81);
#endif
OV2640 cam;

WebServer server(80);

long timer = 0;

String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}

String getContentType(String filename) {
  if (server.hasArg("download")) {
    return "application/octet-stream";
  } else if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}

bool exists(String path){
  bool yes = false;
  File file = FILESYSTEM.open(path, "r");
  if(!file.isDirectory()){
    yes = true;
  }
  file.close();
  return yes;
}

bool loadFromSdCard(String path) {
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) {
    path += "index.htm";
  }
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (exists(pathWithGz) || exists(path)) {
    if (exists(pathWithGz)) {
      path += ".gz";
    }
    File file = FILESYSTEM.open(path, "r");
    Serial.println(file);
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

#ifdef USE_WEBSOCKET
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    Serial.printf("Got event [%u]", type);
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

				// send message to client
				  webSocket.sendTXT(num, "Connected");
            }
            break;
        case WStype_TEXT:
            Serial.printf("[%u] get Text: %s\n", num, payload);

            // send message to client
            // webSocket.sendTXT(num, "message here");

            // send data to all connected clients
            // webSocket.broadcastTXT("message here");
            break;
        case WStype_BIN:
            Serial.printf("[%u] get binary length: %u\n", num, length);
            //hexdump(payload, length);

            // send message to client
            // webSocket.sendBIN(num, payload, length);
            break;
		case WStype_ERROR:			
		case WStype_FRAGMENT_TEXT_START:
		case WStype_FRAGMENT_BIN_START:
		case WStype_FRAGMENT:
		case WStype_FRAGMENT_FIN:
			break;
    }

}
#endif

#ifdef USE_BUTTON
static unsigned long last_interrupt_time = 0;
#endif
void handle_jpg_stream(void)
{
  char buf[32];
  int s;

  WiFiClient client = server.client();

  client.write(HEADER, hdrLen);
  client.write(BOUNDARY, bdrLen);

  while (true)
  {
    if (!client.connected()) break;
    cam.run();
    s = cam.getSize();
    client.write(CTNTTYPE, cntLen);
    sprintf( buf, "%d\r\n\r\n", s );
    client.write(buf, strlen(buf));
    client.write((char *)cam.getfb(), s);
    client.write(BOUNDARY, bdrLen);
  }
}

const char JHEADER[] = "HTTP/1.1 200 OK\r\n" \
                       "Content-disposition: inline; filename=capture.jpg\r\n" \
                       "Content-type: image/jpeg\r\n\r\n";
const int jhdLen = strlen(JHEADER);

void handle_jpg(void)
{
  WiFiClient client = server.client();

  if (!client.connected()) return;
  cam.run();
  client.write(JHEADER, jhdLen);
  client.write((char *)cam.getfb(), cam.getSize());
}

void handle_stream_viewing()
{
  char temp[index_simple_html_len];

   snprintf(temp, index_simple_html_len, index_simple_html);
  server.send(200, "text/html; charset=utf-8", index_simple_html);
}

void handleNotFound()
{
  if (loadFromSdCard(server.uri())) {
    Serial.println("Sending file from SPIFFS");
    return;
  }
  String message = "Server is running!\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  server.send(200, "text/plain", message);
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
  
  server.on("/mjpeg/1", HTTP_GET, handle_jpg_stream);
  server.on("/jpg", HTTP_GET, handle_jpg);
  server.on("/", HTTP_GET, handle_stream_viewing);
  server.onNotFound(handleNotFound);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  server.handleClient();
  webSocket.loop();


  if(millis() - timer > 500) {
    Serial.println("Sending message to websocket client");
    String letme = "LET ME TELL YOU SOMETHING";
    if(webSocket.broadcastTXT(letme)){
      Serial.println("Send message to websocket client");
    }
    timer = millis();
  }

  #ifdef USE_BUTTON
  if (debounce()) {
    servosEnabled = !servosEnabled;
    digitalWrite(buttonLed, servosEnabled);
  }
  #endif
}

