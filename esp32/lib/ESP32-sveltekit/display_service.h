#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <mutex>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

class DisplayService {
  public:
    DisplayService() : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1) {};

    bool begin() {
        std::lock_guard<std::mutex> guard(displayMutex);
        if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
            return false;
        }
        display.clearDisplay();
        return true;
    };

    void clear() {
        std::lock_guard<std::mutex> guard(displayMutex);
        display.clearDisplay();
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) {
        std::lock_guard<std::mutex> guard(displayMutex);
        display.drawPixel(x, y, color);
    }

    void displayMessage(const String &message, int x, int y) {
        std::lock_guard<std::mutex> lock(displayMutex);
        display.setCursor(x, y);
        display.clearDisplay();
        display.println(message);
        display.display();
    }

  private:
    Adafruit_SSD1306 display;
    std::mutex displayMutex;
};
