#pragma once

#include <FastLED.h>
#include <timing.h>
#include <WiFi.h>

#ifndef WS2812_PIN
#define WS2812_PIN 12
#endif

#ifndef WS2812_NUM_LEDS
#define WS2812_NUM_LEDS 1 + 12
#endif

#define COLOR_ORDER GRB
#define CHIPSET WS2811

class LEDService {
  private:
    CRGB leds[WS2812_NUM_LEDS];
    CRGBPalette16 currentPalette;
    TBlendType currentBlending;

    int _brightness = 255;
    int direction = 1;

    void fillFromPallette(CRGBPalette16 colorPalette, uint8_t colorIndex);

  public:
    LEDService();
    ~LEDService();

    void begin();
    void loop();
};