#ifndef LEDService_h
#define LEDService_h

#include <FastLED.h>

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

  public:
    LEDService() {
        FastLED.addLeds<CHIPSET, WS2812_PIN, COLOR_ORDER>(leds, WS2812_NUM_LEDS).setCorrection(TypicalLEDStrip);
        currentPalette = OceanColors_p;
        currentBlending = LINEARBLEND;
    }
    ~LEDService() {}

    void loop() {
        EXECUTE_EVERY_N_MS(1000 / 60, {
            if (_brightness >= 200) direction = -5;
            if (_brightness <= 50) direction = 5;
            _brightness += direction;
            if (WiFi.isConnected()) {
                fillFromPallette(OceanColors_p, 0);
            } else {
                fillFromPallette(ForestColors_p, 128);
            }
            FastLED.show();
        });
    }

    void fillFromPallette(CRGBPalette16 colorPalette, uint8_t colorIndex) {
        CRGB color = ColorFromPalette(colorPalette, colorIndex, _brightness, currentBlending);
        for (int i = 0; i < WS2812_NUM_LEDS; ++i) {
            leds[i] = color;
        }
    }
};

#endif