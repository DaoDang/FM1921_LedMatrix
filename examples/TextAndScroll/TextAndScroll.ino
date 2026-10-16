// P4Panel32 - TextAndScroll
// Shows static text for 5 s, then a scrolling message. 5x7 font, ASCII only (no Vietnamese accents).

#include <P4Panel32.h>

#define NUM_PANELS 1

P4Panel32 panel;
unsigned long t0;
bool scrollStarted = false;

void setup() {
  Serial.begin(115200);
  if (!panel.begin(NUM_PANELS)) {
    Serial.println("panel.begin() failed");
    while (true) delay(1000);
  }
  panel.setBrightness(200);
  panel.drawText(1, 2, "Hello", P4_RED);
  panel.drawText(1, 12, "P4", P4_GREEN, 2);                       // size 2 = double size
  panel.drawTextCentered(26, "32x32", P4Panel32::color(0, 160, 255));
  t0 = millis();
}

void loop() {
  if (!scrollStarted && millis() - t0 > 5000) {
    scrollStarted = true;
    // y = 12, orange, 40 ms per pixel step, size 1
    panel.startScroll("ESP32-S3 + P4 LED panel - RGB, brightness, text, chain!", 12, P4_ORANGE, 40, 1);
  }
  panel.show();
}
