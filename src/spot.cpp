#include <spot.h>

Spot::Spot() 
    : _events(EVENTSOURCE_PATH)
    , _ws(WEBSOCKET_PATH)
    , _server(HTTP_PORT)
    , _display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SCREEN_RESET)
    , _mpu(Wire)
    , _leftUss(USS_LEFT, USS_LEFT, USS_MAX_DISTANCE)
    , _rightUss(USS_RIGHT, USS_RIGHT, USS_MAX_DISTANCE)
    , _pwm(0x40)
{ }

esp_err_t Spot::boot(){
    log_i("Booting...");
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    SPIFFS.begin();
    Wire.begin(SDA, SCL);
    _initialize_button();

    _initialize_display();
    _initialize_mpu();
    _initialize_camera();

    initialize_wifi();
    _initialize_arduino_oat();
    _initialize_server();
    log_i("Done booting");
    return ESP_OK;
}

void Spot::handle(){
    if(USE_CAPTIVE_PORTAL) _dnsServer.processNextRequest();
    ArduinoOTA.handle();
    _ws.cleanupClients();

    broadcast_data();
}

esp_err_t Spot::initialize_wifi(){
    if(USE_CAPTIVE_PORTAL) _initialize_captive_portal();
    else _initialize_wifi_connection();
    if(MDNS.begin(HOSTNAME)){
        MDNS.addService("http", "tcp", HTTP_PORT);
    }

    if(USE_CAPTIVE_PORTAL) display_ip_and_ssid(&_display, WiFi.softAPIP().toString(), HOSTNAME);
    else display_ip_and_ssid(&_display, WiFi.localIP().toString(), SSID);
    return ESP_OK;
}

uint8_t Spot::cpu_temperature(){
    return (temprature_sens_read() - 32) / 1.8;
}

esp_err_t Spot::broadcast_data(){
    if(millis() - _last_broadcast < 50) return ESP_OK;

    size_t numContent = 13;
    float content[numContent];
    content[0] = WiFi.RSSI();
    content[1] = _mpu.getTemp();
    content[2] = _mpu.getAccX();
    content[3] = _mpu.getAccY();
    content[4] = _mpu.getAccZ();
    content[5] = _mpu.getAngleX();
    content[6] = _mpu.getAngleY();
    content[7] = _mpu.getAngleZ();
    content[8] = cpu_temperature();
    content[9] = _leftUss.ping_cm();
    content[10] = _rightUss.ping_cm();
    content[11] = ESP.getFreeHeap();
    content[12] = ESP.getFreePsram();

    uint8_t* buf = (uint8_t*) &content;
    size_t buf_len = sizeof(buf);

    _ws.binaryAll(buf, buf_len * numContent);
    _last_broadcast = millis();

    return ESP_OK;
}

esp_err_t Spot::_initialize_camera(){
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
        camera_config.frame_size = FRAMESIZE_SVGA;
        camera_config.jpeg_quality = 10;
        camera_config.fb_count = 2;
    } else {
        camera_config.frame_size = FRAMESIZE_SVGA;
        camera_config.jpeg_quality = 12;
        camera_config.fb_count = 1;
    }

    log_i("Initializing camera");
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) log_i("Camera probe failed with error 0x%x", err);

    return err;
}

esp_err_t Spot::_initialize_wifi_connection(){
    log_i("Connecting to wifi");
    WiFi.begin(SSID, PASS);
    int8_t timeout = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        timeout++;
        if(timeout > 15) {
            _initialize_captive_portal();
            return ESP_FAIL;
        }
    }
    log_i("Connected successfully");
    return ESP_OK;
}

esp_err_t Spot::_initialize_arduino_oat(){
    log_i("Starting ArduinoOTA");
    ArduinoOTA.onStart([&]() { _events.send("Update Start", "ota"); });
    ArduinoOTA.onEnd([&]() { _events.send("Update End", "ota"); });
    ArduinoOTA.onProgress([&](unsigned int progress, unsigned int total) {
        char p[32];
        sprintf(p, "Progress: %u%%\n", (progress/(total/100)));
        _events.send(p, "ota");
    });
    ArduinoOTA.onError([&](ota_error_t error) {
        if(error == OTA_AUTH_ERROR) _events.send("Auth Failed", "ota");
        else if(error == OTA_BEGIN_ERROR) _events.send("Begin Failed", "ota");
        else if(error == OTA_CONNECT_ERROR) _events.send("Connect Failed", "ota");
        else if(error == OTA_RECEIVE_ERROR) _events.send("Recieve Failed", "ota");
        else if(error == OTA_END_ERROR) _events.send("End Failed", "ota");
    });
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.begin();
    return ESP_OK;
}

esp_err_t Spot::_initialize_captive_portal(){
    log_i("Starting captive portal");
    WiFi.softAP(HOSTNAME);
    _dnsServer.start(53, "*", WiFi.softAPIP());
    return ESP_OK;
}

esp_err_t Spot::_initialize_server(){
    log_i("Starting webserver");
    _ws.onEvent(onWsEvent);
    _server.addHandler(&_ws);
    _server.on("/stream", HTTP_GET, streamJpg);
    _server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    _server.begin();
    return ESP_OK;
}

esp_err_t Spot::_initialize_display(){
    log_i("Initializing display");
    if(!_display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        log_w("SSD1306 allocation failed");
        return ESP_FAIL;
    }

    _display.display();
    delay(200);
    _display.clearDisplay();
    return ESP_OK;
}

esp_err_t Spot::_initialize_mpu(){
    log_i("Initializing MPU");
    byte status = _mpu.begin();
    if(status != 0) {
        log_w("MPU initialize failed");
        return ESP_FAIL;
    }
    log_i("Calculating offsets, do not move MPU6050");
    delay(1000);
    log_i("Cone calculating offsets");
    _mpu.calcOffsets(true,true);
    return ESP_OK;
}

esp_err_t Spot::_initialize_pwm_controller(){
    log_i("Initializing PWM controller");
    _pwm.begin();
    _pwm.setOscillatorFrequency(SERVO_OSCILLATOR_FREQUENCY);
    _pwm.setPWMFreq(SERVO_FREQ);
    return ESP_OK;
}

esp_err_t Spot::_initialize_button() {
    pinMode(BUTTON_LED, OUTPUT);
    digitalWrite(BUTTON_LED, HIGH);
    //pinMode(BUTTON, INPUT);
    return ESP_OK;
}

void display_ip_and_ssid(Adafruit_SSD1306* display, String ip, const char* ssid) {
    display->setTextColor(WHITE, BLACK);
    display->setTextSize(1);
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t h = 0;
    uint16_t w = 0;
    
    display->getTextBounds(ssid, 0, 0, &x1, &y1, &w, &h);
    display->setCursor(SCREEN_WIDTH/2 - w/2, SCREEN_HEIGHT/2-8 - h/2);
    display->println(ssid);

    display->getTextBounds(ip, 0, 0, &x1, &y1, &w, &h);
    display->setCursor(SCREEN_WIDTH/2 - w/2, SCREEN_HEIGHT/2+8 - h/2);
    display->println(ip);
    display->display();
}