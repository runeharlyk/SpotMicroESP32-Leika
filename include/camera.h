#include <esp_camera.h>
#include <Arduino.h>

#include <config.h>
#include <camera_pins.h>

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
      camera_config.frame_size = FRAMESIZE_SVGA;
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