/*
 * User settings
 */
#define HOSTNAME "Leika"
#define SSID ""
#define PASS ""


/*
 * Server settings
 */
#define HTTP_PORT 80
#define WEBSOCKET_PATH "/"
#define EVENTSOURCE_PATH "/events"


/*
 * Camera module
 */
#define CAMERA_MODEL_AI_THINKER
// #define CAMERA_MODEL_WROVER_KIT
// #define CAMERA_MODEL_ESP_EYE
// #define CAMERA_MODEL_M5STACK_PSRAM
// #define CAMERA_MODEL_M5STACK_V2_PSRAM
// #define CAMERA_MODEL_M5STACK_WIDE
// #define CAMERA_MODEL_M5STACK_ESP32CAM
// #define CAMERA_MODEL_TTGO_T_JOURNAL
// #define CAMERA_MODEL_ARDUCAM_ESP32S_UNO


/*
 * OLED Settings
 */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_RESET -1


/*
 * I2C software connection 
 */
#define SDA 14
#define SCL 15


/*
 * Serial settings
 */
#define BAUDRATE 115200
#define SERIAL_DEBUG_OUTPUT true


/*
 * Ultra sonic sensors
 */
#define USS_LEFT 12
#define USS_RIGHT 13
#define USS_MAX_DISTANCE 200

/*
 * Button settings
 */
#define BUTTON 16
#define BUTTON_LED 2

/*
 * PWM controller settings
 */
#define SERVO_OSCILLATOR_FREQUENCY 27000000
#define SERVO_FREQ 50