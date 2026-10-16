// P4Panel32 - driver for P4 32x32 RGB LED panels (label e.g. "FM1921 ... 16S2")
// whose row select behaves like a shift register (A = DCLK, C = SDIN).
// Chainable (N modules in series), 24-bit colour via BCM + gamma, brightness,
// 5x7 text and scrolling text.  Target: ESP32 family (tested: ESP32-S3).
//
// MIT licence - see LICENSE.
#ifndef P4PANEL32_H
#define P4PANEL32_H

#include <Arduino.h>

#define P4PANEL32_VERSION "1.0.0"

// GPIO assignment. r1..b2 (colour data) MUST be GPIO 0..31 (fast set/clear
// uses one 32-bit register). a,b,c,d,lat,oe,clk may be any output GPIO.
struct P4Pins {
  int8_t r1, g1, b1, r2, g2, b2;
  int8_t a, b, c, d;
  int8_t lat, oe, clk;
};

// R1=4 G1=5 B1=6 R2=7 G2=15 B2=16 A=18 B=8 C=3 D=42 LAT=40 OE=2 CLK=41
P4Pins P4DefaultPins();

// Handy colours (0xRRGGBB)
static const uint32_t P4_BLACK   = 0x000000;
static const uint32_t P4_RED     = 0xFF0000;
static const uint32_t P4_GREEN   = 0x00FF00;
static const uint32_t P4_BLUE    = 0x0000FF;
static const uint32_t P4_YELLOW  = 0xFFFF00;
static const uint32_t P4_CYAN    = 0x00FFFF;
static const uint32_t P4_MAGENTA = 0xFF00FF;
static const uint32_t P4_WHITE   = 0xFFFFFF;
static const uint32_t P4_ORANGE  = 0xFF8000;

class P4Panel32 {
public:
  enum {
    PANEL_W    = 32,   // pixels per module (width)
    PANEL_H    = 32,   // pixels per module (height)
    MAX_PLANES = 8,    // max bit planes (colour depth per channel)
    MAX_PANELS = 16    // hard limit on chained modules (RAM ~ 7 KB per module)
  };

  P4Panel32();
  ~P4Panel32();

  // ---- life cycle ----------------------------------------------------
  // panels: number of modules in series (1..MAX_PANELS).
  // Returns false on invalid pins or out of memory.
  bool begin(uint8_t panels = 1, const P4Pins* pins = NULL);
  void end();
  // Change the number of chained modules at run time (clears the image).
  bool setPanels(uint8_t n);

  int width()  const { return W_; }
  int height() const { return PANEL_H; }
  int panels() const { return nPanels_; }

  // ---- running ---------------------------------------------------------
  // Call as often as possible (every loop()). = update() + refresh().
  void show();
  // Advance scrolling text and convert the image to bit planes if it changed.
  void update();
  // Scan ONE full frame (blocks ~ a few ms).
  void refresh();
  // Force the conversion image -> bit planes (normally automatic).
  void commit();

  // ---- drawing (x = 0 is the FARTHEST module from the ESP32, left side) -
  void clear();
  void fill(uint32_t rgb);
  void drawPixel(int x, int y, uint32_t rgb);
  void drawPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
  void drawHLine(int x, int y, int w, uint32_t rgb);
  void drawVLine(int x, int y, int h, uint32_t rgb);
  void drawLine(int x0, int y0, int x1, int y1, uint32_t rgb);
  void drawRect(int x, int y, int w, int h, uint32_t rgb);   // outline
  void fillRect(int x, int y, int w, int h, uint32_t rgb);
  void drawColorTest();                                      // hue ramp + grey ramp

  // 5x7 font, ASCII only. Each glyph occupies 6*size pixels horizontally.
  void drawChar(int x, int y, char ch, uint32_t rgb, int size = 1);
  void drawText(int x, int y, const char* s, uint32_t rgb, int size = 1);
  void drawTextCentered(int y, const char* s, uint32_t rgb, int size = 1);
  int  textWidth(const char* s, int size = 1) const;

  // Right-to-left scrolling text (processed in update()/show()).
  void startScroll(const char* text, int y, uint32_t rgb, uint16_t msPerStep = 40, int size = 1);
  void stopScroll() { scrolling_ = false; }
  bool scrolling() const { return scrolling_; }

  // ---- colours -----------------------------------------------------------
  static uint32_t color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  }
  static uint32_t colorHSV(int hue /*0..359*/, uint8_t sat, uint8_t val);
  // Accepts "0".."7", "#RRGGBB", "r,g,b" or a name (RED, GREEN, ... ORANGE, PINK, PURPLE, OFF)
  static bool parseColor(const char* s, uint32_t* rgb);

  // ---- appearance --------------------------------------------------------
  void  setBrightness(uint8_t b)      { bri_ = b; }             // 0..255 (time based)
  uint8_t brightness() const          { return bri_; }
  void  setGamma(float g);                                       // 1.0 .. 4.0, default 2.2
  float gamma() const                 { return gamma10_ / 10.0f; }
  void  setColorDepth(uint8_t planes);                           // 1..8 bit planes, default 5
  uint8_t colorDepth() const          { return planes_; }
  void  setUnitTime(uint16_t us);                                // LSB on-time, 1..50 us, default 10
  uint16_t unitTime() const           { return unitUs_; }
  void  setClockDelay(uint8_t nops);                             // NOPs per clock phase, default 3
  uint8_t clockDelay() const          { return spd_; }
  void  setFlipX(bool f)              { flipX_ = f; dirty_ = true; }
  void  setFlipY(bool f)              { flipY_ = f; dirty_ = true; }
  bool  flipX() const                 { return flipX_; }
  bool  flipY() const                 { return flipY_; }

  // ---- row-scan model (leave default unless your panel differs) ----------
  // Lines are 0=A 1=B 2=C 3=D. Default: DCLK = A (0), SDIN = C (2).
  bool  setRowLines(uint8_t dclkLine, uint8_t sdinLine);
  uint8_t dclkLine() const            { return dclkSel_; }
  uint8_t sdinLine() const            { return sdinSel_; }
  void  setMarkerStep(uint8_t k)      { mk_ = k & 15; }
  uint8_t markerStep() const          { return mk_; }
  void  setInvertRowData(bool inv)    { inv_ = inv; }
  bool  invertRowData() const         { return inv_; }
  void  setChainLength(uint8_t n)     { chainLen_ = n < 16 ? 16 : (n > 64 ? 64 : n); }
  uint8_t chainLength() const         { return chainLen_; }

  const P4Pins& pins() const          { return pin_; }

private:
  struct Rgb { uint8_t r, g, b; };

  void buildGamma();
  void buildMasks();
  void shiftPlane(const uint8_t* m);
  int  linePin(int line) const;
  void updateScroll();

  Rgb*      fb_;                   // [PANEL_H][W_]
  uint8_t*  plane_;                // [MAX_PLANES][16][W_], 6 bit: R1 G1 B1 R2 G2 B2
  int       nPanels_;
  int       W_;
  int       planes_;
  int       unitUs_;
  int       bri_;
  int       gamma10_;
  int       spd_;
  int       dclkSel_;
  int       sdinSel_;
  int       mk_;
  bool      inv_;
  int       chainLen_;
  bool      flipX_;
  bool      flipY_;
  bool      dirty_;
  bool      begun_;

  bool      scrolling_;
  char      scrText_[120];
  int       scrX_, scrY_, scrMs_, scrSize_;
  uint32_t  scrColor_;
  unsigned long lastScr_;

  P4Pins    pin_;
  uint32_t  clkSetReg_, clkClrReg_, clkBit_;
  uint16_t  gam_[256];
  uint32_t  setMask_[64], clrMask_[64];

  P4Panel32(const P4Panel32&);              // not copyable
  P4Panel32& operator=(const P4Panel32&);
};

#endif
