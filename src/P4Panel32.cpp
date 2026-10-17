#include "P4Panel32.h"
#include "P4Font5x7.h"
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

P4Pins P4DefaultPins() {
  P4Pins p = {4, 5, 6, 7, 15, 16, 18, 8, 3, 42, 40, 2, 41};
  return p;
}

// ---------------------------------------------------------------- GPIO
static inline void gpioHi(int pin) {
  if (pin < 32) REG_WRITE(GPIO_OUT_W1TS_REG, 1UL << pin);
  else          REG_WRITE(GPIO_OUT1_W1TS_REG, 1UL << (pin - 32));
}
static inline void gpioLo(int pin) {
  if (pin < 32) REG_WRITE(GPIO_OUT_W1TC_REG, 1UL << pin);
  else          REG_WRITE(GPIO_OUT1_W1TC_REG, 1UL << (pin - 32));
}
static inline void nopn(int n) { for (int i = 0; i < n; i++) __asm__ __volatile__("nop"); }

// ---------------------------------------------------------------- life cycle
P4Panel32::P4Panel32()
  : fb_(NULL), plane_(NULL), nPanels_(0), W_(0), planes_(5), unitUs_(10), bri_(255),
    gamma10_(22), spd_(3), dclkSel_(0), sdinSel_(2), mk_(0), inv_(false), chainLen_(16),
    flipX_(false), flipY_(false), dirty_(true), begun_(false), font_(NULL),
    clipOn_(false), clipX0_(0), clipY0_(0), clipX1_(0), clipY1_(0),
    clkSetReg_(0), clkClrReg_(0), clkBit_(0) {
  pin_ = P4DefaultPins();
  memset(scr_, 0, sizeof(scr_));
  buildGamma();
}

P4Panel32::~P4Panel32() { end(); }

bool P4Panel32::begin(uint8_t panels, const P4Pins* pins) {
  P4Pins p = pins ? *pins : P4DefaultPins();
  const int8_t data[6] = {p.r1, p.g1, p.b1, p.r2, p.g2, p.b2};
  for (int i = 0; i < 6; i++) if (data[i] < 0 || data[i] > 31) return false;   // must be bank 0
  const int8_t other[7] = {p.a, p.b, p.c, p.d, p.lat, p.oe, p.clk};
  for (int i = 0; i < 7; i++) if (other[i] < 0 || other[i] > 63) return false;

  pin_ = p;
  const int8_t all[13] = {p.r1, p.g1, p.b1, p.r2, p.g2, p.b2, p.a, p.b, p.c, p.d, p.lat, p.oe, p.clk};
  for (int i = 0; i < 13; i++) { pinMode(all[i], OUTPUT); digitalWrite(all[i], LOW); }
  digitalWrite(p.oe, HIGH);                                   // LEDs off

  clkSetReg_ = (p.clk < 32) ? GPIO_OUT_W1TS_REG : GPIO_OUT1_W1TS_REG;
  clkClrReg_ = (p.clk < 32) ? GPIO_OUT_W1TC_REG : GPIO_OUT1_W1TC_REG;
  clkBit_    = 1UL << (p.clk & 31);

  buildGamma();
  buildMasks();
  begun_ = true;
  if (!setPanels(panels)) { begun_ = false; return false; }
  return true;
}

void P4Panel32::end() {
  if (begun_) digitalWrite(pin_.oe, HIGH);
  begun_ = false;
  free(fb_);    fb_ = NULL;
  free(plane_); plane_ = NULL;
  nPanels_ = 0; W_ = 0;
}

bool P4Panel32::setPanels(uint8_t n) {
  int np = clampi(n, 1, MAX_PANELS);
  int w = PANEL_W * np;
  Rgb*     f = (Rgb*)calloc((size_t)PANEL_H * w, sizeof(Rgb));
  uint8_t* q = (uint8_t*)calloc((size_t)MAX_PLANES * 16 * w, 1);
  if (!f || !q) { free(f); free(q); return false; }
  free(fb_); free(plane_);
  fb_ = f; plane_ = q; nPanels_ = np; W_ = w;
  stopScroll();
  clipOn_ = false;
  dirty_ = true;
  return true;
}

// ---------------------------------------------------------------- tables
void P4Panel32::buildGamma() {
  float g = gamma10_ / 10.0f;
  for (int v = 0; v < 256; v++) {
    float f = powf(v / 255.0f, g);
    gam_[v] = (uint16_t)(f * 65535.0f + 0.5f);
  }
}

void P4Panel32::buildMasks() {
  const int dp[6] = {pin_.r1, pin_.g1, pin_.b1, pin_.r2, pin_.g2, pin_.b2};
  uint32_t all = 0;
  for (int i = 0; i < 6; i++) all |= (1UL << dp[i]);
  for (int m = 0; m < 64; m++) {
    uint32_t s = 0;
    for (int i = 0; i < 6; i++) if (m & (1 << i)) s |= (1UL << dp[i]);
    setMask_[m] = s;
    clrMask_[m] = all & ~s;
  }
}

int P4Panel32::linePin(int line) const {
  switch (line) {
    case 0:  return pin_.a;
    case 1:  return pin_.b;
    case 2:  return pin_.c;
    default: return pin_.d;
  }
}

// ---------------------------------------------------------------- settings
void P4Panel32::setGamma(float g) {
  gamma10_ = clampi((int)(g * 10.0f + 0.5f), 10, 40);
  buildGamma();
  dirty_ = true;
}
void P4Panel32::setColorDepth(uint8_t p) { planes_ = clampi(p, 1, MAX_PLANES); dirty_ = true; }
void P4Panel32::setUnitTime(uint16_t us) { unitUs_ = clampi(us, 1, 50); }
void P4Panel32::setClockDelay(uint8_t n) { spd_ = clampi(n, 0, 40); }
bool P4Panel32::setRowLines(uint8_t d, uint8_t s) {
  if (d > 3 || s > 3 || d == s) return false;
  dclkSel_ = d; sdinSel_ = s;
  return true;
}

// ---------------------------------------------------------------- drawing
void P4Panel32::clear() {
  if (fb_) memset(fb_, 0, (size_t)PANEL_H * W_ * sizeof(Rgb));
  dirty_ = true;
}

void P4Panel32::drawPixel(int x, int y, uint32_t c) {
  if (!fb_ || x < 0 || x >= W_ || y < 0 || y >= PANEL_H) return;
  if (clipOn_ && (x < clipX0_ || x >= clipX1_ || y < clipY0_ || y >= clipY1_)) return;
  Rgb& p = fb_[y * W_ + x];
  p.r = (c >> 16) & 255; p.g = (c >> 8) & 255; p.b = c & 255;
  dirty_ = true;
}
void P4Panel32::drawPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) { drawPixel(x, y, color(r, g, b)); }

void P4Panel32::setClip(int x, int y, int w, int h) {
  clipOn_ = true;
  clipX0_ = x; clipY0_ = y; clipX1_ = x + w; clipY1_ = y + h;   // w or h <= 0: nothing can be drawn
}
void P4Panel32::clearClip() { clipOn_ = false; }

void P4Panel32::fillRect(int x, int y, int w, int h, uint32_t c) {
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) drawPixel(x + i, y + j, c);
}
void P4Panel32::fill(uint32_t c) { fillRect(0, 0, W_, PANEL_H, c); }
void P4Panel32::drawHLine(int x, int y, int w, uint32_t c) { fillRect(x, y, w, 1, c); }
void P4Panel32::drawVLine(int x, int y, int h, uint32_t c) { fillRect(x, y, 1, h, c); }

void P4Panel32::drawRect(int x, int y, int w, int h, uint32_t c) {
  if (w <= 0 || h <= 0) return;
  drawHLine(x, y, w, c);
  drawHLine(x, y + h - 1, w, c);
  drawVLine(x, y, h, c);
  drawVLine(x + w - 1, y, h, c);
}

void P4Panel32::drawLine(int x0, int y0, int x1, int y1, uint32_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    drawPixel(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// ---------------------------------------------------------------- text (UTF-8)
// Decode one code point and advance `s` past it. Invalid, overlong or truncated
// sequences (and stray continuation bytes) give '?'; a truncated sequence resumes at the
// offending byte, so the rest of the string is still decoded.
static uint32_t utf8Next(const char*& s) {
  const uint8_t* p = (const uint8_t*)s;
  uint32_t c = *p++;
  if (c < 0x80) { s = (const char*)p; return c; }
  int n; uint32_t cp, minCp;
  if      ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; minCp = 0x80; }
  else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; minCp = 0x800; }
  else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; minCp = 0x10000; }
  else { s = (const char*)p; return '?'; }
  for (int i = 0; i < n; i++) {
    if ((*p & 0xC0) != 0x80) { s = (const char*)p; return '?'; }
    cp = (cp << 6) | (*p++ & 0x3F);
  }
  s = (const char*)p;
  if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return '?';
  return cp;
}

// Glyph of the font for a code point; falls back to '?' when the font lacks it.
static const P4Glyph* findGlyph(const P4Font* f, uint32_t cp) {
  const P4Glyph* g = P4FontFind(f, cp);
  return g ? g : P4FontFind(f, '?');
}

// Draws one glyph with its cell top-left at (x, y); returns the horizontal advance in pixels.
int P4Panel32::drawGlyphWith(const P4Font* f, int x, int y, uint32_t cp, uint32_t c, int sz) {
  if (sz < 1) sz = 1;
  if (!f) {                                          // built-in 5x7, ASCII only
    if (cp < 32 || cp > 126) cp = '?';
    const uint8_t* g = &P4_FONT5x7[(cp - 32) * 5];
    for (int cx = 0; cx < 5; cx++) {
      uint8_t bits = g[cx];
      for (int cy = 0; cy < 7; cy++)
        if (bits & (1 << cy)) fillRect(x + cx * sz, y + cy * sz, sz, sz, c);
    }
    return 6 * sz;
  }
  const P4Glyph* g = findGlyph(f, cp);
  if (!g) return 0;
  const uint8_t* bm = f->bitmap + g->offset;
  const int bx = x + g->dx * sz;
  const int by = y + (f->ascent + g->dy) * sz;
  int bit = 0;
  for (int r = 0; r < g->h; r++)
    for (int k = 0; k < g->w; k++, bit++)
      if (bm[bit >> 3] & (0x80 >> (bit & 7))) fillRect(bx + k * sz, by + r * sz, sz, sz, c);
  return g->adv * sz;
}

int P4Panel32::drawGlyph(int x, int y, uint32_t cp, uint32_t c, int sz) {
  return drawGlyphWith(font_, x, y, cp, c, sz);
}

void P4Panel32::drawChar(int x, int y, char ch, uint32_t c, int sz) {
  drawGlyphWith(font_, x, y, (uint8_t)ch, c, sz);
}

// Width of the ink-relevant part of the text: the sum of the advances minus the empty space
// after the last glyph (so centred text looks centred).
int P4Panel32::textWidthWith(const P4Font* f, const char* s, int sz) const {
  if (sz < 1) sz = 1;
  if (!f) {
    int n = 0;
    while (*s) { utf8Next(s); n++; }
    return n ? n * 6 * sz - sz : 0;
  }
  int w = 0, tail = 0;
  while (*s) {
    const P4Glyph* g = findGlyph(f, utf8Next(s));
    if (!g) continue;
    w += g->adv;
    tail = g->adv - (g->dx + g->w);
  }
  if (tail < 0) tail = 0;
  return w > tail ? (w - tail) * sz : 0;
}

int P4Panel32::textWidth(const char* s, int sz) const { return textWidthWith(font_, s, sz); }

int P4Panel32::drawTextWith(const P4Font* f, int x, int y, const char* s, uint32_t c, int sz) {
  if (sz < 1) sz = 1;
  while (*s) x += drawGlyphWith(f, x, y, utf8Next(s), c, sz);
  return x;
}

int P4Panel32::drawText(int x, int y, const char* s, uint32_t c, int sz) {
  return drawTextWith(font_, x, y, s, c, sz);
}

void P4Panel32::drawTextCentered(int y, const char* s, uint32_t c, int sz) {
  drawText((W_ - textWidth(s, sz)) / 2, y, s, c, sz);
}

uint32_t P4Panel32::colorHSV(int h, uint8_t s, uint8_t v) {
  h %= 360; if (h < 0) h += 360;
  int region = h / 60, rem = (h % 60) * 255 / 60;
  int p = v * (255 - s) / 255;
  int q = v * (255 - (s * rem) / 255) / 255;
  int t = v * (255 - (s * (255 - rem)) / 255) / 255;
  switch (region) {
    case 0:  return color(v, t, p);
    case 1:  return color(q, v, p);
    case 2:  return color(p, v, t);
    case 3:  return color(p, q, v);
    case 4:  return color(t, p, v);
    default: return color(v, p, q);
  }
}

void P4Panel32::drawColorTest() {
  clear();
  for (int x = 0; x < W_; x++) {
    int hue = x * 360 / W_;
    uint8_t v = (uint8_t)(x * 255 / (W_ > 1 ? W_ - 1 : 1));
    for (int y = 0;  y < 16; y++) drawPixel(x, y, colorHSV(hue, 255, 255));   // hue ramp
    for (int y = 16; y < 24; y++) drawPixel(x, y, color(v, v, v));            // grey ramp
    for (int y = 24; y < 32; y++) drawPixel(x, y, colorHSV(hue, 255, v));     // dimming ramp
  }
}

bool P4Panel32::parseColor(const char* s, uint32_t* out) {
  static const uint32_t basic[8] = {0x000000, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
  struct N { const char* n; uint32_t c; };
  static const N names[] = {
    {"OFF", 0x000000}, {"RED", 0xFF0000}, {"GREEN", 0x00FF00}, {"BLUE", 0x0000FF},
    {"YELLOW", 0xFFFF00}, {"CYAN", 0x00FFFF}, {"MAGENTA", 0xFF00FF}, {"WHITE", 0xFFFFFF},
    {"ORANGE", 0xFF8000}, {"PINK", 0xFF69B4}, {"PURPLE", 0x8000FF}
  };
  if (!s || !*s) return false;
  if (s[0] == '#') {
    if (strlen(s + 1) != 6) return false;
    char* e; unsigned long v = strtoul(s + 1, &e, 16);
    if (*e) return false;
    *out = (uint32_t)v; return true;
  }
  if (strchr(s, ',')) {
    int r, g, b;
    if (sscanf(s, "%d,%d,%d", &r, &g, &b) != 3) return false;
    *out = color((uint8_t)clampi(r, 0, 255), (uint8_t)clampi(g, 0, 255), (uint8_t)clampi(b, 0, 255));
    return true;
  }
  if (isdigit((unsigned char)s[0])) {
    int n = atoi(s);
    if (n > 7) return false;
    *out = basic[n]; return true;
  }
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    if (strcasecmp(s, names[i].n) == 0) { *out = names[i].c; return true; }
  return false;
}

// ---------------------------------------------------------------- scrolling text
// Copy a UTF-8 string into a fixed buffer; when it has to be cut, never inside a multi-byte character.
static void copyUtf8(char* dst, size_t cap, const char* src) {
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
  size_t n = strlen(dst);
  if (strlen(src) > n) {                                                     // text was cut
    size_t i = n;
    while (i > 0 && ((uint8_t)dst[i - 1] & 0xC0) == 0x80) i--;               // skip continuation bytes
    if (i > 0 && (uint8_t)dst[i - 1] >= 0xC0) {                              // dst[i-1] = lead byte
      uint8_t lead = (uint8_t)dst[i - 1];
      size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
      if (n - (i - 1) < need) dst[i - 1] = 0;                                // partial character: drop it
    }
  }
}

int P4Panel32::startScrollSlot(int slot, int rx, int ry, int rw, int rh, int ty,
                               const char* text, uint32_t c, uint16_t ms, int sz) {
  Scroller& s = scr_[slot];
  s.active = false;
  copyUtf8(s.text, sizeof(s.text), text);
  s.font = font_;
  s.rx = rx; s.ry = ry; s.rw = rw; s.rh = rh; s.ty = ty;
  s.ms = clampi(ms, 5, 2000); s.size = clampi(sz, 1, 4); s.color = c;
  s.textW = textWidthWith(s.font, s.text, s.size);
  s.x = rx + rw;                                   // enters from the right edge of the area
  s.last = 0;
  s.active = true;
  return slot;
}

void P4Panel32::startScroll(const char* text, int y, uint32_t c, uint16_t ms, int sz) {
  stopScroll();
  startScrollSlot(0, 0, 0, W_, PANEL_H, y, text, c, ms, sz);
}

int P4Panel32::startScrollIn(int x, int y, int w, int h, const char* text, uint32_t c, uint16_t ms, int sz) {
  if (w <= 0 || h <= 0) return -1;
  int slot = -1;
  for (int i = 0; i < MAX_SCROLLS && slot < 0; i++) {                  // same area: replace its text
    const Scroller& s = scr_[i];
    if (s.active && s.rx == x && s.ry == y && s.rw == w && s.rh == h) slot = i;
  }
  for (int i = 0; i < MAX_SCROLLS && slot < 0; i++) if (!scr_[i].active) slot = i;
  if (slot < 0) return -1;
  int ty = y + (h - fontHeight() * clampi(sz, 1, 4)) / 2;              // text line cell centred in the area
  return startScrollSlot(slot, x, y, w, h, ty, text, c, ms, sz);
}

void P4Panel32::stopScroll() {
  for (int i = 0; i < MAX_SCROLLS; i++) scr_[i].active = false;
}
void P4Panel32::stopScroll(int slot) {
  if (slot >= 0 && slot < MAX_SCROLLS) scr_[slot].active = false;
}
bool P4Panel32::scrolling() const {
  for (int i = 0; i < MAX_SCROLLS; i++) if (scr_[i].active) return true;
  return false;
}

void P4Panel32::updateScroll() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_SCROLLS; i++) {
    Scroller& s = scr_[i];
    if (!s.active || now - s.last < (unsigned long)s.ms) continue;
    s.last = now;
    const bool on = clipOn_;                                           // the caller's clip, restored below
    const int a = clipX0_, b = clipY0_, c = clipX1_, d = clipY1_;
    setClip(s.rx, s.ry, s.rw, s.rh);
    if (s.rx <= 0 && s.ry <= 0 && s.rw >= W_ && s.rh >= PANEL_H) clear();
    else fillRect(s.rx, s.ry, s.rw, s.rh, P4_BLACK);
    drawTextWith(s.font, s.x, s.ty, s.text, s.color, s.size);
    clipOn_ = on; clipX0_ = a; clipY0_ = b; clipX1_ = c; clipY1_ = d;
    s.x--;
    if (s.x < s.rx - s.textW) s.x = s.rx + s.rw;                       // gone: start again from the right edge
  }
}

// ---------------------------------------------------------------- image -> bit planes
void P4Panel32::commit() {
  if (!fb_) return;
  const int P = planes_;
  const int sh = 16 - P;
  const uint32_t rnd = sh > 0 ? (1u << (sh - 1)) : 0;
  const uint32_t qmax = (1u << P) - 1;
  for (int y = 0; y < 16; y++) {
    int yt = flipY_ ? 31 - y : y;
    int yb = flipY_ ? 15 - y : y + 16;
    for (int x = 0; x < W_; x++) {
      int sx = flipX_ ? (W_ - 1 - x) : x;
      const Rgb& t = fb_[yt * W_ + sx];
      const Rgb& b = fb_[yb * W_ + sx];
      uint32_t q[6];
      q[0] = (gam_[t.r] + rnd) >> sh;  q[1] = (gam_[t.g] + rnd) >> sh;  q[2] = (gam_[t.b] + rnd) >> sh;
      q[3] = (gam_[b.r] + rnd) >> sh;  q[4] = (gam_[b.g] + rnd) >> sh;  q[5] = (gam_[b.b] + rnd) >> sh;
      for (int c = 0; c < 6; c++) if (q[c] > qmax) q[c] = qmax;
      for (int p = 0; p < P; p++) {
        uint8_t m = 0;
        for (int c = 0; c < 6; c++) m |= ((q[c] >> p) & 1) << c;
        plane_[((size_t)p * 16 + y) * W_ + x] = m;
      }
    }
  }
  dirty_ = false;
}

// ---------------------------------------------------------------- scanning
void P4Panel32::shiftPlane(const uint8_t* m) {
  for (int i = 0; i < W_; i++) {
    uint8_t v = m[i];
    REG_WRITE(GPIO_OUT_W1TS_REG, setMask_[v]);
    REG_WRITE(GPIO_OUT_W1TC_REG, clrMask_[v]);
    nopn(spd_);
    REG_WRITE(clkSetReg_, clkBit_);
    nopn(spd_);
    REG_WRITE(clkClrReg_, clkBit_);
    nopn(spd_);
  }
}

void P4Panel32::refresh() {
  if (!begun_ || !fb_) return;
  uint32_t tp[MAX_PLANES];
  for (int p = 0; p < planes_; p++) tp[p] = (((uint32_t)unitUs_ << p) * (uint32_t)bri_ + 127) / 255;   // rounded

  const int dclk = linePin(dclkSel_);
  const int sdin = linePin(sdinSel_);

  for (int k = 0; k < 16; k++) {
    gpioHi(pin_.oe);
    int marker = (k == mk_) ? 1 : 0;
    if ((marker != 0) != inv_) gpioHi(sdin); else gpioLo(sdin);
    delayMicroseconds(1);
    gpioHi(dclk); delayMicroseconds(1); gpioLo(dclk);           // shift the lit row

    int y = ((k - mk_) % 16 + 16) % 16;
    for (int p = 0; p < planes_; p++) {
      if (tp[p] == 0) continue;
      shiftPlane(plane_ + ((size_t)p * 16 + y) * W_);
      gpioHi(pin_.lat); delayMicroseconds(1); gpioLo(pin_.lat);
      gpioLo(pin_.oe);
      delayMicroseconds(tp[p]);
      gpioHi(pin_.oe);
    }
  }
  if (chainLen_ > 16) {                                         // longer row chain: extra DCLK, LEDs off
    gpioHi(pin_.oe);
    if (inv_) gpioHi(sdin); else gpioLo(sdin);
    for (int i = 16; i < chainLen_; i++) {
      gpioHi(dclk); delayMicroseconds(2); gpioLo(dclk); delayMicroseconds(2);
    }
  }
  if (bri_ == 0) delay(5);
}

void P4Panel32::update() {
  updateScroll();
  if (dirty_) commit();
}

void P4Panel32::show() {
  update();
  refresh();
}
