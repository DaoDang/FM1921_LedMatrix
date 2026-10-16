// P4Panel32 - BasicDrawing
// Draws a frame, diagonals, colour bars and a moving dot on 1..N chained P4 32x32 modules.
// Wiring (default): R1=4 G1=5 B1=6 R2=7 G2=15 B2=16 A=18 B=8 C=3 D=42 LAT=40 OE=2 CLK=41
// Chain: ESP32 -> IN of module 1 (right side seen from the front); OUT of module 1 -> IN of module 2 ...
// x = 0 is the left edge of the LAST module (farthest from the ESP32).

#include <P4Panel32.h>

#define NUM_PANELS 1          // set to the number of modules in series

P4Panel32 panel;

void setup() {
  Serial.begin(115200);
  if (!panel.begin(NUM_PANELS)) {               // optional 2nd argument: custom P4Pins
    Serial.println("panel.begin() failed (bad pins or out of memory)");
    while (true) delay(1000);
  }
  panel.setBrightness(255);                     // 0..255

  const int W = panel.width(), H = panel.height();
  panel.drawRect(0, 0, W, H, P4_BLUE);
  panel.drawLine(0, 0, W - 1, H - 1, P4_RED);
  panel.drawLine(0, H - 1, W - 1, 0, P4_GREEN);
  panel.fillRect(2, 2, 6, 6, P4Panel32::color(255, 128, 0));      // orange
  panel.fillRect(W - 8, H - 8, 6, 6, P4Panel32::colorHSV(280, 255, 255));
  panel.drawTextCentered(12, "P4", P4_WHITE);
}

void loop() {
  // moving dot on row 20
  static unsigned long last = 0;
  static int x = 0;
  if (millis() - last > 30) {
    last = millis();
    panel.drawPixel(x, 20, P4_BLACK);
    x = (x + 1) % panel.width();
    panel.drawPixel(x, 20, P4_YELLOW);
  }
  panel.show();                                  // must be called continuously
}
