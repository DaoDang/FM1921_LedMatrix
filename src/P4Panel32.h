// P4Panel32 - driver for P4 32x32 RGB LED panels (label e.g. "FM1921 ... 16S2")
// whose row select behaves like a shift register (A = DCLK, C = SDIN).
// Chainable (N modules in series), 24-bit colour via BCM + gamma, brightness,
// text (built-in 5x7 or UTF-8 bitmap fonts, e.g. Vietnamese) and scrolling text.
// Target: ESP32 family (tested: ESP32-S3).
//
// MIT licence - see LICENSE.
#ifndef P4PANEL32_H
#define P4PANEL32_H

#include <Arduino.h>
#include "P4Font.h"

#define P4PANEL32_VERSION "1.3.0"
#define P4PANEL32_HAS_SCROLL_FRAMES 1   // setScrollFrames() / frames() available
#define P4PANEL32_HAS_PREPARE 1      // prepare() / swapIfReady() available (double-buffered bit planes)

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
    MAX_PANELS = 16,   // hard limit on chained modules (RAM ~ 7 KB per module)
    MAX_SCROLLS = 4    // scrolling-text areas that can run at the same time
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
  // One thread / simple sketches: call show() as often as possible (every loop()). = update() + refresh().
  void show();
  // Advance scrolling text and convert the image to bit planes if it changed (= prepare() + swapIfReady()).
  void update();
  // Scan ONE full frame (blocks ~ a few ms).
  void refresh();
  // Frame-locked scrolling: step every n scan frames instead of every `ms` milliseconds (0 = use ms).
  // Steps then land on whole frames, so the motion has no 6/7-frame jitter. frames() counts refresh() calls.
  void     setScrollFrames(uint8_t n) { scrollFrames_ = n; }
  uint32_t frames() const { return frames_; }
  // Force the conversion image -> bit planes straight into the picture being scanned (normally automatic).
  void commit();

  // Two threads / two cores (e.g. FreeRTOS tasks): the conversion image -> bit planes takes about a millisecond
  // and, done inside the scanning loop, makes the LEDs go dark for that time at every change (a scrolling text
  // changes the picture ~25 times a second: visible flicker of static text). With two bit-plane buffers the
  // conversion runs in the DRAWING thread while the SCANNING thread keeps scanning the previous picture:
  //   scanning thread:  loop { panel.swapIfReady(); panel.refresh(); }          // never blocked
  //   drawing thread:   draw..., panel.prepare();   // call it often (every few ms): moves scrolling text, converts
  // All drawing calls, setClip(), startScrollIn() and prepare() must come from the same (drawing) thread.
  // begin()/end()/setPanels() and the set...() settings must not run while the other thread is scanning.
  // prepare() returns true when a new picture was produced; it waits (returns false) until the previous one has
  // been taken over by swapIfReady(). Do not mix with commit(). Falls back to a single buffer if RAM is short.
  bool prepare();
  void swapIfReady();

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

  // ---- display areas (clipping) --------------------------------------------
  // setClip() limits ALL drawing (pixels, shapes, text) to the rectangle (x, y, w, h), so something
  // drawn for one area can never spill into another. clearClip() removes the limit.
  // clear() is not affected (it always clears the whole panel); clearRect() blacks out one area.
  void setClip(int x, int y, int w, int h);
  void clearClip();
  void clearRect(int x, int y, int w, int h) { fillRect(x, y, w, h, P4_BLACK); }

  // ---- text ---------------------------------------------------------------
  // Strings are UTF-8. y is the TOP of the line cell (baseline = y + fontAscent()).
  // `size` scales the font by an integer factor (glyph pixels become size x size blocks).
  // Default font: built-in 5x7, ASCII only (each glyph 6*size px wide; other characters -> '?').
  // With setFont() the text can contain any character the font has (e.g. Vietnamese).
  void drawChar(int x, int y, char ch, uint32_t rgb, int size = 1);
  int  drawGlyph(int x, int y, uint32_t codepoint, uint32_t rgb, int size = 1);   // returns the advance in px
  int  drawText(int x, int y, const char* s, uint32_t rgb, int size = 1);         // returns the x after the last glyph
  void drawTextCentered(int y, const char* s, uint32_t rgb, int size = 1);
  int  textWidth(const char* s, int size = 1) const;

  // Bitmap font for drawChar/drawText/startScroll, e.g. setFont(&DejaVuVN_12) after
  // #include <DejaVuVN_12.h>  (fonts are made by extras/fontgen/make_vn_font.py).
  // setFont(NULL) = built-in 5x7 font (default). The font must stay valid (use the generated ones).
  void setFont(const P4Font* f)       { font_ = f; }
  const P4Font* font() const          { return font_; }
  int  fontHeight() const             { return font_ ? font_->height : 7; }   // line cell height, size 1
  int  fontAscent() const             { return font_ ? font_->ascent : 7; }   // top of cell -> baseline, size 1

  // ---- scrolling text (right to left, processed in update()/show()) ---------
  // Both use the font that is set when they are called and move one pixel every msPerStep.
  //
  // startScroll(): the whole panel is the scroll area (it is cleared at every step), the text line
  //   cell starts at row y. One scroller; it stops any other running scroller.
  void startScroll(const char* text, int y, uint32_t rgb, uint16_t msPerStep = 40, int size = 1);
  // startScrollIn(): scrolls inside the rectangle (x, y, w, h) ONLY. Only that rectangle is redrawn,
  //   the rest of the panel (e.g. a static clock line) is not touched, and the text is clipped to it.
  //   The text line cell is centred vertically in the rectangle (top = y + (h - fontHeight()*size) / 2).
  //   Up to MAX_SCROLLS rectangles run at the same time. Starting a scroll for the same rectangle
  //   again replaces its text (restarts from the right edge). Returns the slot 0..MAX_SCROLLS-1,
  //   or -1 if the rectangle is empty or all slots are in use. Do not mix with startScroll().
  int  startScrollIn(int x, int y, int w, int h, const char* text, uint32_t rgb,
                     uint16_t msPerStep = 40, int size = 1);
  void stopScroll();                  // stop all scrollers (the last picture stays on the panel)
  void stopScroll(int slot);          // stop one slot (value returned by startScrollIn)
  bool scrolling() const;             // true while any scroller runs

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
  struct Scroller {
    bool          active;
    const P4Font* font;
    char          text[240];         // UTF-8 (Vietnamese letters take 2-3 bytes)
    int           rx, ry, rw, rh;    // scroll area: cleared and clipped at every step
    int           ty;                // top of the text line cell
    int           x;                 // current x of the start of the text
    int           textW;             // text width, cached
    int           ms, size;
    uint32_t      color;
    unsigned long last;
    uint32_t      lastFrame;         // frame counter at the last step (frame-locked scrolling)
  };

  void buildGamma();
  void buildLut();
  void convertInto(uint8_t* dst);
  void buildMasks();
  void shiftPlane(const uint8_t* m);
  int  linePin(int line) const;
  void updateScroll();
  int  drawGlyphWith(const P4Font* f, int x, int y, uint32_t cp, uint32_t rgb, int size);
  int  drawTextWith(const P4Font* f, int x, int y, const char* s, uint32_t rgb, int size);
  int  textWidthWith(const P4Font* f, const char* s, int size) const;
  int  startScrollSlot(int slot, int rx, int ry, int rw, int rh, int ty,
                       const char* text, uint32_t rgb, uint16_t ms, int size);

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
  const P4Font* font_;

  bool      clipOn_;
  int       clipX0_, clipY0_, clipX1_, clipY1_;   // clip rectangle, x1/y1 exclusive
  Scroller  scr_[MAX_SCROLLS];

  P4Pins    pin_;
  uint32_t  clkSetReg_, clkClrReg_, clkBit_;
  uint16_t  gam_[256];
  uint32_t  setMask_[64], clrMask_[64];
  // commit() lookup tables: lut_[c][v] = bits of channel c (R1 G1 B1 R2 G2 B2) of 8-bit value v, already
  // gamma-corrected, quantised to planes_ bits and placed in the 6-bit field of each of planes 0..4.
  uint32_t  lut_[6][256];
  uint8_t   q_[256];                 // gamma + quantisation of one 8-bit value (used for planes 5..7)
  bool      lutOk_;
  uint8_t*  planeBack_;              // second bit-plane buffer (picture being prepared), NULL = single buffer
  volatile uint32_t frames_ = 0;     // completed refresh() calls (written by the scanning thread)
  uint8_t  scrollFrames_ = 0;
  volatile bool backReady_;          // planeBack_ holds a finished picture that swapIfReady() has not taken yet

  P4Panel32(const P4Panel32&);              // not copyable
  P4Panel32& operator=(const P4Panel32&);
};

#endif
