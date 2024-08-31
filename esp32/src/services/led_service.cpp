#include <services/peripherals/led_service.h>

LEDService::LEDService() {
    FastLED.addLeds<CHIPSET, WS2812_PIN, COLOR_ORDER>(leds, WS2812_NUM_LEDS).setCorrection(TypicalLEDStrip);
    currentPalette = OceanColors_p;
    currentBlending = LINEARBLEND;
}

LEDService::~LEDService() {}

void LEDService::begin() {}

void LEDService::loop() {
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

void LEDService::fillFromPallette(CRGBPalette16 colorPalette, uint8_t colorIndex) {
    CRGB color = ColorFromPalette(colorPalette, colorIndex, _brightness, currentBlending);
    for (int i = 0; i < WS2812_NUM_LEDS; ++i) {
        leds[i] = color;
    }
}
