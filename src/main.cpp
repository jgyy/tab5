#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] Tab5 idle game scaffold booting");

    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(20, 20);
    M5.Display.println("Hello Tab5");

    Serial.println("[BOOT] Display initialized, showing Hello Tab5");
}

void loop() {
    M5.update();
    delay(10);
}
