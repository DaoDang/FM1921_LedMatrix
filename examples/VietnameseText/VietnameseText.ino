// P4Panel32 - VietnameseText
// Vietnamese (UTF-8) text with the DejaVu bitmap fonts: a 16 px line on top, a 9 px line at the
// bottom, then a long message scrolling in the big font.
// Save the sketch as UTF-8 (the default in Arduino IDE and VS Code) and type the accents
// precomposed (the usual Unikey / Telex output).
//
// Fonts: DejaVuVN_9, DejaVuVN_12, DejaVuVN_16 (other sizes: extras/fontgen/make_vn_font.py).
// Line cell heights: 9 px -> 13 rows, 12 px -> 17 rows, 16 px -> 21 rows (panel.fontHeight()).
// The cell leaves room for the tallest capitals with stacked marks (Ấ, Ẫ ...); ordinary text is
// shorter, so a 16 px line at y = 0 and a 9 px line at y = 32 - 13 do not touch.

#include <P4Panel32.h>
#include <DejaVuVN_9.h>
#include <DejaVuVN_16.h>

#define NUM_PANELS 2          // 2 modules = 64 x 32. With 1 module only very short words fit - scroll instead.

P4Panel32 panel;
unsigned long phaseStart;
bool scrolling = false;

static void showStatic() {
  panel.stopScroll();
  panel.clear();
  panel.setFont(&DejaVuVN_16);                                 // big line on top
  panel.drawTextCentered(0, "Hà Nội", P4_YELLOW);
  panel.setFont(&DejaVuVN_9);                                  // small line, bottom aligned
  panel.drawTextCentered(32 - panel.fontHeight(), "Việt Nam", P4_CYAN);
}

static void showScroll() {
  panel.setFont(&DejaVuVN_16);                                 // startScroll() remembers the current font
  int y = (32 - panel.fontHeight()) / 2;                       // vertically centred
  panel.startScroll("Chào mừng bạn đến với Việt Nam – ESP32-S3 + P4 LED panel!", y, P4_ORANGE, 30);
}

void setup() {
  Serial.begin(115200);
  if (!panel.begin(NUM_PANELS)) {
    Serial.println("panel.begin() failed");
    while (true) delay(1000);
  }
  panel.setBrightness(200);
  showStatic();
  phaseStart = millis();
}

void loop() {
  // 5 s static two-line text, then 20 s scrolling text, and again
  unsigned long now = millis();
  if (!scrolling && now - phaseStart > 5000) {
    scrolling = true; phaseStart = now; showScroll();
  } else if (scrolling && now - phaseStart > 20000) {
    scrolling = false; phaseStart = now; showStatic();
  }
  panel.show();
}
