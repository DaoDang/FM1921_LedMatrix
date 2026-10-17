// Host emulator test of the P4Panel32 library (internal consistency, NOT real hardware).
#include "Arduino.h"
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include <vector>
unsigned long g_us = 0; SerialC Serial;
#define private public         // white-box access for the test only
#include "P4Panel32.h"
#include "P4Console.h"
#undef private
#include "DejaVuVN_9.h"
#include "DejaVuVN_12.h"
#include "DejaVuVN_16.h"


// ---- emulator of the fitted panel model; pins are configurable ----
static uint64_t gp = 0;
static std::vector<int> shreg[6], latched[6];
static int rowS[16] = {0};
static double lum[32][256][3];
static unsigned long oeFallAt = 0; static bool oeLow = false;
static int Wemu = 32;
static int PD[6], P_DCLK, P_SDIN, P_OE, P_CLK, P_LAT;
void emu_reg_write(uint32_t reg, uint32_t v) {
  uint64_t o = gp;
  if (reg == 1) gp |= v; else if (reg == 2) gp &= ~(uint64_t)v;
  else if (reg == 3) gp |= ((uint64_t)v << 32); else gp &= ~((uint64_t)v << 32);
  auto rise = [&](int pin) { return !((o >> pin) & 1) && ((gp >> pin) & 1); };
  if (rise(P_CLK)) for (int c = 0; c < 6; c++) { int b = (gp >> PD[c]) & 1; shreg[c].insert(shreg[c].begin(), b); if ((int)shreg[c].size() > Wemu) shreg[c].pop_back(); }
  if (rise(P_LAT)) for (int c = 0; c < 6; c++) latched[c] = shreg[c];
  if (rise(P_DCLK)) { int sd = (gp >> P_SDIN) & 1; for (int i = 15; i > 0; i--) rowS[i] = rowS[i-1]; rowS[0] = sd; }
  bool oeNow = (gp >> P_OE) & 1, oeOld = (o >> P_OE) & 1;
  if (oeOld && !oeNow) { oeLow = true; oeFallAt = g_us; }
  if (!oeOld && oeNow && oeLow) {
    oeLow = false; double d = g_us - oeFallAt;
    for (int i = 0; i < 16; i++) if (rowS[i]) for (int x = 0; x < Wemu; x++) {
      int pos = Wemu - 1 - x; if (pos >= (int)latched[0].size()) continue;
      for (int c = 0; c < 3; c++) { lum[i][x][c] += d * latched[c][pos]; lum[i+16][x][c] += d * latched[3+c][pos]; }
    }
  }
}
static void emuConfig(const P4Panel32* /*unused*/, const P4Pins& p, int dclkPin, int sdinPin, int W) {
  int d[6] = {p.r1, p.g1, p.b1, p.r2, p.g2, p.b2};
  for (int i = 0; i < 6; i++) PD[i] = d[i];
  P_DCLK = dclkPin; P_SDIN = sdinPin; P_OE = p.oe; P_CLK = p.clk; P_LAT = p.lat; Wemu = W;
  gp = 0; for (int c = 0; c < 6; c++) { shreg[c].clear(); latched[c].clear(); } for (int i = 0; i < 16; i++) rowS[i] = 0;
  oeLow = false;
}

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static uint32_t quant(const P4Panel32& P, uint8_t v) {
  int sh = 16 - P.planes_; uint32_t q = (P.gam_[v] + (sh > 0 ? (1u << (sh - 1)) : 0)) >> sh; uint32_t qm = (1u << P.planes_) - 1; return q > qm ? qm : q;
}
static void checkAll(P4Panel32& P, const char* name, bool fx = false, bool fy = false) {
  P.update();
  memset(lum, 0, sizeof(lum)); P.refresh(); memset(lum, 0, sizeof(lum)); P.refresh();
  int bad = 0; int W = P.W_;
  for (int y = 0; y < 32; y++) for (int x = 0; x < W; x++) {
    int sx = fx ? W - 1 - x : x; int sy = fy ? 31 - y : y;
    const P4Panel32::Rgb& p = P.fb_[sy * W + sx];
    double e[3] = {(double)quant(P, p.r) * P.unitUs_ * P.bri_ / 255.0, (double)quant(P, p.g) * P.unitUs_ * P.bri_ / 255.0, (double)quant(P, p.b) * P.unitUs_ * P.bri_ / 255.0};
    for (int c = 0; c < 3; c++) if (fabs(lum[y][x][c] - e[c]) > 0.5 * (1 << P.planes_) * P.unitUs_ / 255.0 + 0.5 * P.planes_ + 1e-6) { if (bad < 5) printf("  mismatch %s x=%d y=%d c=%d got %.1f exp %.1f\n", name, x, y, c, lum[y][x][c], e[c]); bad++; }
  }
  printf("%s: %d mismatches\n", name, bad); if (bad) fails++;
}
static void cmd(P4Console& con, const char* s) { Serial.q.push_back(s); con.handle(); }

// ---- helpers for the font / UTF-8 tests (independent reference implementations) ----
static uint32_t refU8(const char*& s) {          // strict decoder for VALID strings only
  const unsigned char* p = (const unsigned char*)s; uint32_t c = *p++;
  if (c < 0x80) { s = (const char*)p; return c; }
  int n = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : 3;
  uint32_t cp = c & (0x3F >> n);
  while (n--) cp = (cp << 6) | (*p++ & 0x3F);
  s = (const char*)p; return cp;
}
static bool validUtf8(const char* s) {
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    int n = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
    if (n < 0) return false;
    p++;
    while (n--) { if ((*p & 0xC0) != 0x80) return false; p++; }
  }
  return true;
}
static int refWidth(const P4Font* f, const char* s) {       // sum of advances minus the empty tail of the last glyph
  int w = 0, tail = 0;
  while (*s) { const P4Glyph* g = P4FontFind(f, refU8(s)); w += g->adv; tail = g->adv - (g->dx + g->w); }
  if (tail < 0) tail = 0;
  return w - tail;
}
static std::vector<char> snap(const P4Panel32& P) {          // lit pixels of the whole frame buffer
  std::vector<char> v; for (int i = 0; i < 32 * P.W_; i++) v.push_back(P.fb_[i].r | P.fb_[i].g | P.fb_[i].b ? 1 : 0); return v;
}
static int litCount(const std::vector<char>& v) { int n = 0; for (char c : v) n += c; return n; }
static void inkRows(const P4Panel32& P, int* top, int* bottom) {
  *top = 99; *bottom = -1;
  for (int y = 0; y < 32; y++) for (int x = 0; x < P.W_; x++)
    if (P.fb_[y * P.W_ + x].r | P.fb_[y * P.W_ + x].g | P.fb_[y * P.W_ + x].b) { if (y < *top) *top = y; if (y > *bottom) *bottom = y; }
}
#define CHECK_FONT(F) do { \
    const P4Font& f = F; char m[96]; \
    snprintf(m, sizeof m, #F ": codes sorted, one glyph per code"); \
    bool ok = f.count == sizeof(F##_codes) / sizeof(F##_codes[0]) && f.count == sizeof(F##_glyphs) / sizeof(F##_glyphs[0]); \
    for (int i = 1; i < f.count; i++) if (f.codes[i] <= f.codes[i - 1]) ok = false; \
    CHECK(ok, m); \
    snprintf(m, sizeof m, #F ": bitmap offsets and glyph boxes inside the cell"); \
    ok = f.ascent <= f.height; \
    for (int i = 0; i < f.count; i++) { const P4Glyph& g = f.glyphs[i]; \
      if (g.offset + (g.w * g.h + 7) / 8 > (int)sizeof(F##_bitmap)) ok = false; \
      if ((g.w * g.h) != 0 && (f.ascent + g.dy < 0 || f.ascent + g.dy + g.h > f.height)) ok = false; } \
    CHECK(ok, m); } while (0)

int main() {
  // ---------- default pins ----------
  {
    P4Panel32 P; P4Pins pp = P4DefaultPins();
    CHECK(P.begin(1), "begin default");
    emuConfig(&P, pp, pp.a, pp.c, 32);
    P.drawPixel(0, 0, 0xFF0000); P.drawPixel(31, 31, 0x00FF00); P.drawPixel(5, 20, 0x336699);
    P.drawPixel(10, 3, 0xFFFFFF); P.drawPixel(20, 16, 0x808080); P.drawPixel(15, 15, 0x010203);
    P.drawPixel(40, 40, 0xFFFFFF);   // out of range ignored
    checkAll(P, "1 module mk0");
    P.setMarkerStep(5); checkAll(P, "1 module mk5"); P.setMarkerStep(0);
    P.setBrightness(128); checkAll(P, "bri128"); P.setBrightness(255);
    P.setColorDepth(3); checkAll(P, "3 planes"); P.setColorDepth(8); checkAll(P, "8 planes"); P.setColorDepth(5);
    P.setGamma(1.0f); checkAll(P, "gamma 1.0"); P.setGamma(2.2f);
    P.setFlipX(true); checkAll(P, "flipX", true, false); P.setFlipX(false);
    P.setFlipY(true); checkAll(P, "flipY", false, true); P.setFlipY(false);

    // 2 modules
    CHECK(P.setPanels(2), "setPanels 2"); emuConfig(&P, pp, pp.a, pp.c, 64);
    CHECK(P.width() == 64 && P.panels() == 2, "width 64");
    P.drawPixel(63, 31, 0xFF8000); P.drawPixel(0, 0, 0, 0, 255); P.drawPixel(32, 10, P4_WHITE);
    checkAll(P, "2 modules");
    P.drawColorTest(); checkAll(P, "color test 2 modules");
    P.clear(); P.drawRect(0, 0, 64, 32, P4_BLUE); P.drawLine(0, 0, 63, 31, P4_RED);
    P.drawHLine(3, 5, 10, P4_GREEN); P.drawVLine(7, 2, 9, P4_YELLOW); P.fillRect(50, 20, 5, 5, P4_CYAN);
    CHECK(P.fb_[0 * 64 + 63].b == 255 && P.fb_[31 * 64 + 0].b == 255, "rect corners");
    CHECK(P.fb_[5 * 64 + 12].g == 255 && P.fb_[5 * 64 + 13].g == 0, "hline extent");
    CHECK(P.fb_[10 * 64 + 7].r == 255 && P.fb_[10 * 64 + 7].g == 255, "vline");
    checkAll(P, "shapes 2 modules");

    // text
    P.clear(); P.drawText(1, 4, "HELLO", P4_RED);
    printf("--- text 'HELLO' at (1,4) ---\n");
    for (int y = 2; y < 13; y++) { for (int x = 0; x < 34; x++) putchar(P.fb_[y * 64 + x].r ? '#' : '.'); putchar('\n'); }
    P.clear(); P.drawTextCentered(20, "Hi", P4_GREEN);
    int minx = 999, maxx = -1; for (int x = 0; x < 64; x++) for (int y = 20; y < 27; y++) if (P.fb_[y * 64 + x].g) { if (x < minx) minx = x; if (x > maxx) maxx = x; }
    printf("ctr 'Hi' spans x=%d..%d\n", minx, maxx); CHECK(abs((minx + maxx) / 2 - 31) <= 1, "centered");
    CHECK(P.textWidth("Hi", 2) == 22, "textWidth");

    // scroll
    P.startScroll("TEST", 12, P4_YELLOW, 40, 1); CHECK(P.scrolling(), "scrolling");
    int seenMin = 999;
    for (int i = 0; i < 300; i++) { g_us += 41000; P.update(); if (P.scr_[0].x < seenMin) seenMin = P.scr_[0].x; }
    CHECK(seenMin < 0 && P.scr_[0].x <= 64, "scroll moved & wrapped"); P.stopScroll(); CHECK(!P.scrolling(), "stop");

    // ---------- Vietnamese / UTF-8 fonts ----------
    {
      CHECK_FONT(DejaVuVN_9); CHECK_FONT(DejaVuVN_12); CHECK_FONT(DejaVuVN_16);
      CHECK(DejaVuVN_9.height == 13 && DejaVuVN_12.height == 17 && DejaVuVN_16.height == 21, "font cell heights");

      // every Vietnamese letter (67 lower + 67 upper) and the extras exist in every font
      const char* vn =
        "àáảãạăằắẳẵặâầấẩẫậèéẻẽẹêềếểễệìíỉĩịòóỏõọôồốổỗộơờớởỡợùúủũụưừứửữựỳýỷỹỵđ"
        "ÀÁẢÃẠĂẰẮẲẴẶÂẦẤẨẪẬÈÉẺẼẸÊỀẾỂỄỆÌÍỈĨỊÒÓỎÕỌÔỒỐỔỖỘƠỜỚỞỠỢÙÚỦŨỤƯỪỨỬỮỰỲÝỶỸỴĐ"
        "°±×÷·–—‘’“”…•₫€£©";
      const P4Font* fonts[3] = {&DejaVuVN_9, &DejaVuVN_12, &DejaVuVN_16};
      int missing = 0, letters = 0;
      for (int k = 0; k < 3; k++) for (const char* s = vn; *s; ) { uint32_t cp = refU8(s); if (!P4FontFind(fonts[k], cp)) missing++; if (k == 0) letters++; }
      CHECK(missing == 0 && letters == 134 + 17, "all Vietnamese letters + extras in all fonts");
      for (uint32_t cp = 32; cp < 127; cp++) CHECK(P4FontFind(&DejaVuVN_12, cp) != NULL, "ASCII glyph present");

      // setFont / metrics / default font
      P.setFont(&DejaVuVN_12);
      CHECK(P.font() == &DejaVuVN_12 && P.fontHeight() == 17 && P.fontAscent() == 14, "setFont metrics");
      P.setFont(NULL);
      CHECK(P.font() == NULL && P.fontHeight() == 7 && P.textWidth("Hi", 2) == 22, "setFont(NULL) = 5x7");

      // the library's UTF-8 path gives the same width as the reference decoder, and scales with size
      const char* tests[] = {"Hà Nội", "Việt Nam", "Chào mừng bạn đến với Việt Nam – ESP32-S3!", "Ấ", "x"};
      for (int k = 0; k < 3; k++) {
        P.setFont(fonts[k]);
        for (const char* t : tests) {
          char m[160]; snprintf(m, sizeof m, "textWidth font %d: %s", k, t);
          CHECK(P.textWidth(t) == refWidth(fonts[k], t), m);
          CHECK(P.textWidth(t, 2) == 2 * P.textWidth(t, 1), "textWidth scales with size");
        }
      }
      CHECK(P.textWidth("") == 0, "empty text width");

      // a precomposed letter draws the base letter plus its marks (dot below, circumflex above)
      P.setFont(&DejaVuVN_12);
      P.clear(); P.drawText(0, 0, "e", P4_WHITE); int t1, b1; inkRows(P, &t1, &b1); int n1 = litCount(snap(P));
      P.clear(); P.drawText(0, 0, "ệ", P4_WHITE); int t2, b2; inkRows(P, &t2, &b2); int n2 = litCount(snap(P));
      CHECK(n2 > n1 && t2 < t1 && b2 > b1, "ệ has marks above and below e");

      // invalid UTF-8 never crashes and shows '?'
      P.setFont(&DejaVuVN_12);
      struct { const char* bad; const char* same; const char* what; } inv[] = {
        {"\xC3", "?", "truncated at end"}, {"\x80", "?", "stray continuation"}, {"\xFF", "?", "invalid lead"},
        {"\xC3(", "?(", "truncated, resumes"}, {"\xC0\x80", "?", "overlong"}, {"\xED\xA0\x80", "?", "surrogate"},
        {"\xF0\x9F\x98\x80", "?", "emoji not in font"}, {"\xE4\xB8\xAD", "?", "CJK not in font"},
      };
      for (auto& c : inv) {
        P.clear(); P.drawText(0, 0, c.bad, P4_WHITE); std::vector<char> a = snap(P);
        P.clear(); P.drawText(0, 0, c.same, P4_WHITE); std::vector<char> b = snap(P);
        char m[96]; snprintf(m, sizeof m, "invalid UTF-8: %s", c.what); CHECK(a == b && litCount(a) > 0, m);
      }
      // built-in 5x7 font: one UTF-8 character that it lacks is ONE '?' (not one per byte)
      P.setFont(NULL);
      CHECK(P.textWidth("é") == 5 && P.textWidth("Việt") == 4 * 6 - 1, "5x7 counts characters, not bytes");
      { P.clear(); P.drawText(0, 0, "Việt", P4_WHITE); std::vector<char> a = snap(P); P.clear(); P.drawText(0, 0, "Vi?t", P4_WHITE); CHECK(a == snap(P), "5x7: a UTF-8 character it lacks becomes one '?'"); }

      // example layout: 16 px line on top (y = 0), 9 px line at the bottom (y = 32 - height), no overlap
      P.setFont(&DejaVuVN_16); P.clear(); P.drawTextCentered(0, "Hà Nội", P4_YELLOW); int tt, tb; inkRows(P, &tt, &tb);
      CHECK(P.textWidth("Hà Nội") <= 64, "example top line fits 64 px");
      P.setFont(&DejaVuVN_9);  P.clear(); P.drawTextCentered(32 - P.fontHeight(), "Việt Nam", P4_CYAN); int bt, bb; inkRows(P, &bt, &bb);
      CHECK(P.textWidth("Việt Nam") <= 64, "example bottom line fits 64 px");
      printf("example rows: top line %d..%d, bottom line %d..%d\n", tt, tb, bt, bb);
      CHECK(tt >= 0 && tb < bt && bb <= 31, "example lines do not overlap and stay on the panel");
      // the scrolling message stays inside the 32 rows when centred with the 16 px font
      { const char* msg = "Chào mừng bạn đến với Việt Nam – ESP32-S3 + P4 LED panel!";
        int y = (32 - DejaVuVN_16.height) / 2; bool inside = true;
        for (const char* s = msg; *s; ) { const P4Glyph* g = P4FontFind(&DejaVuVN_16, refU8(s)); int top = y + DejaVuVN_16.ascent + g->dy; if ((g->w * g->h) != 0 && (top < 0 || top + g->h > 32)) inside = false; }
        CHECK(inside, "scroll message inside the panel rows"); }

      // scrolling Vietnamese text: font is remembered, long text is cut on a character boundary
      P.setFont(&DejaVuVN_16);
      std::string longText; while (longText.size() < 400) longText += "Việt Nam ";
      P.startScroll(longText.c_str(), 5, P4_YELLOW, 40);
      CHECK(P.scr_[0].font == &DejaVuVN_16 && validUtf8(P.scr_[0].text) && strlen(P.scr_[0].text) > 200, "startScroll keeps font, cuts at a UTF-8 boundary");
      P.setFont(NULL);                                                 // later setFont() does not change a running scroll
      P.startScroll("Hà Nội", 5, P4_YELLOW, 40);                      // 5x7 now
      CHECK(P.scr_[0].font == NULL, "scroll font follows setFont at call time");
      P.setFont(&DejaVuVN_16); P.startScroll("Hà Nội", 5, P4_YELLOW, 40); P.setFont(NULL);
      int seenMin2 = 999; for (int i = 0; i < 400; i++) { g_us += 41000; P.update(); if (P.scr_[0].x < seenMin2) seenMin2 = P.scr_[0].x; }
      CHECK(seenMin2 <= -P.textWidthWith(&DejaVuVN_16, "Hà Nội", 1) && P.scr_[0].x <= 64, "scroll with font scrolls off and wraps");
      P.stopScroll();
      P.setFont(NULL);
    }

    // ---------- display areas: clip, clearRect, drawText return value, scrolling areas ----------
    {
      CHECK(P.setPanels(2), "areas: 2 modules");
      // clipping
      P.setFont(NULL); P.clear(); P.setClip(10, 10, 5, 4); P.fill(P4_WHITE);
      { std::vector<char> v = snap(P); bool ok = litCount(v) == 20;
        for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) if (v[y * 64 + x] && !(x >= 10 && x < 15 && y >= 10 && y < 14)) ok = false;
        CHECK(ok, "setClip limits a fill to the rectangle"); }
      P.clearClip(); P.clear(); P.drawText(0, 0, "HELLO", P4_WHITE); std::vector<char> full = snap(P);
      P.clear(); P.setClip(0, 0, 10, 32); P.drawText(0, 0, "HELLO", P4_WHITE); std::vector<char> part = snap(P);
      { bool ok = litCount(part) > 0 && litCount(part) < litCount(full);
        for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) if (part[y * 64 + x] != (x < 10 ? full[y * 64 + x] : 0)) ok = false;
        CHECK(ok, "clipped text = the visible part of the unclipped text"); }
      P.clear(); P.setClip(5, 5, 0, 10); P.fill(P4_WHITE); CHECK(litCount(snap(P)) == 0, "empty clip draws nothing");
      P.clearClip(); P.fill(P4_WHITE); P.setClip(0, 0, 4, 4); P.clear(); CHECK(litCount(snap(P)) == 0, "clear() ignores the clip");
      P.clearClip(); P.fill(P4_WHITE); P.clearRect(8, 8, 4, 4); CHECK(litCount(snap(P)) == 64 * 32 - 16, "clearRect blacks out one area");
      P.clear();

      // drawText returns the pen position after the last glyph
      CHECK(P.drawText(3, 0, "ABC", P4_WHITE) == 3 + 18 && P.drawText(0, 0, "", P4_WHITE) == 0 && P.drawText(0, 0, "Việt", P4_WHITE) == 24,
            "drawText returns the pen x (5x7, characters not bytes)");
      P.setFont(&DejaVuVN_16);
      { const char* t = "17:30"; int adv = 0; for (const char* q = t; *q; ) adv += P4FontFind(&DejaVuVN_16, refU8(q))->adv;
        CHECK(P.drawText(2, 0, t, P4_WHITE) == 2 + adv, "drawText returns the pen x (font)");
        int x = P.drawText(2, 0, "17", P4_WHITE); x = P.drawText(x, 0, ":", P4_BLACK); x = P.drawText(x, 0, "30", P4_WHITE);
        CHECK(x == 2 + adv, "split drawing HH / colon / MM keeps the layout"); }
      { int w0 = 0; for (const char* q = "00:00"; *q; ) w0 += P4FontFind(&DejaVuVN_16, refU8(q))->adv;
        bool same = true; const char* times[] = {"11:11", "12:34", "23:59", "08:07", "19:45", "20:00"};
        for (const char* t : times) { int w = 0; for (const char* q = t; *q; ) w += P4FontFind(&DejaVuVN_16, refU8(q))->adv; if (w != w0) same = false; }
        CHECK(same, "digits are tabular in DejaVuVN_16 (the clock does not jump)"); }

      // scrolling inside an area
      P.setFont(&DejaVuVN_9); P.stopScroll(); P.clear(); P.fillRect(0, 0, 64, 15, P4_BLUE);       // static band, rows 0..14
      std::vector<char> top0 = snap(P);
      int s0 = P.startScrollIn(0, 19, 64, 13, "DL 17-10-2026", P4_CYAN, 40);
      CHECK(s0 == 0 && P.scrolling() && P.scr_[0].ty == 19, "startScrollIn: slot 0; a 13-row area holds the 13-row cell");
      { bool outsideOk = true, inkSeen = false; int minX = 999, maxX = -1;
        for (int i = 0; i < 600; i++) {
          g_us += 41000; P.update();
          for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) {
            const P4Panel32::Rgb& p = P.fb_[y * 64 + x]; bool lit = (p.r | p.g | p.b) != 0;
            if (y < 19) { if (lit != (top0[y * 64 + x] != 0)) outsideOk = false; }
            else if (lit) { inkSeen = true; if (x < minX) minX = x; if (x > maxX) maxX = x; }
          }
        }
        CHECK(outsideOk, "scroll area never touches the rest of the panel");
        CHECK(inkSeen && minX == 0 && maxX == 63, "scroll text crosses the whole width of its area"); }

      // slots
      { int s1 = P.startScrollIn(0, 0, 64, 10, "a", P4_RED), s2 = P.startScrollIn(0, 10, 64, 9, "b", P4_RED), s3 = P.startScrollIn(10, 0, 20, 5, "c", P4_RED);
        CHECK(s1 == 1 && s2 == 2 && s3 == 3, "new areas take slots 1, 2, 3");
        CHECK(P.startScrollIn(1, 1, 5, 5, "d", P4_RED) == -1, "no free slot -> -1");
        CHECK(P.startScrollIn(0, 10, 64, 9, "bb", P4_RED) == 2 && strcmp(P.scr_[2].text, "bb") == 0, "same area replaces its text (same slot)");
        CHECK(P.startScrollIn(0, 0, 0, 5, "x", P4_RED) == -1 && P.startScrollIn(0, 0, 5, -1, "x", P4_RED) == -1, "empty area -> -1");
        P.stopScroll(2); CHECK(!P.scr_[2].active && P.scr_[1].active && P.scrolling(), "stopScroll(slot) stops one area");
        P.stopScroll(99); P.stopScroll(-1); CHECK(P.scr_[1].active && P.scr_[3].active, "stopScroll(bad slot) is ignored");
        CHECK(P.startScrollIn(1, 1, 5, 5, "d", P4_RED) == 2, "a freed slot is reused");
        P.startScroll("legacy", 5, P4_GREEN, 40);
        CHECK(P.scr_[0].active && !P.scr_[1].active && !P.scr_[2].active && !P.scr_[3].active && P.scr_[0].rw == 64 && P.scr_[0].rh == 32 && P.scr_[0].ty == 5,
              "startScroll = one whole-panel scroller, text top at y");
        P.stopScroll(); CHECK(!P.scrolling(), "stopScroll() stops all"); }

      // vertical centring of the text cell in the area
      P.setFont(&DejaVuVN_16); P.startScrollIn(0, 0, 64, 32, "x", P4_RED, 40, 1);
      CHECK(P.scr_[0].ty == (32 - 21) / 2, "text cell centred in the area (16 px font)");
      P.stopScroll(); P.setFont(NULL); P.startScrollIn(0, 4, 64, 20, "x", P4_RED, 40, 2);
      CHECK(P.scr_[0].ty == 4 + (20 - 14) / 2, "text cell centred in the area (5x7 at size 2)");
      P.stopScroll();

      // text taller / wider than its area stays inside it
      P.clear(); P.setFont(&DejaVuVN_16); P.startScrollIn(8, 12, 30, 4, "HÀ NỘI 2026", P4_WHITE, 40, 1);
      { bool clipOk = true, any = false;
        for (int i = 0; i < 300; i++) { g_us += 41000; P.update(); std::vector<char> v = snap(P);
          for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) if (v[y * 64 + x]) { any = true; if (x < 8 || x >= 38 || y < 12 || y >= 16) clipOk = false; } }
        CHECK(any && clipOk, "oversized text stays inside its area"); }
      P.stopScroll();

      // start at the right edge of the area, leave at the left edge, wrap
      P.clear(); P.setFont(&DejaVuVN_9); P.startScrollIn(10, 0, 20, 13, "Hi", P4_WHITE, 40);
      { CHECK(P.scr_[0].x == 30, "scroll starts at the right edge of its area");
        int tw = P.scr_[0].textW, minx = 999, prev = P.scr_[0].x; bool wrapped = false;
        for (int i = 0; i < 400; i++) { g_us += 41000; P.update(); int x = P.scr_[0].x; if (x < minx) minx = x; if (x > prev) wrapped = true; prev = x; }
        CHECK(minx <= 10 - tw && wrapped && P.scr_[0].x <= 30, "scroll leaves at the left edge and wraps"); }

      // the caller's clip survives an update(); setPanels() resets clip and scrollers
      P.stopScroll(); P.clear(); P.setFont(NULL); P.startScrollIn(0, 20, 64, 8, "x", P4_RED, 40);
      P.setClip(2, 2, 3, 3); g_us += 41000; P.update();
      CHECK(P.clipOn_ && P.clipX0_ == 2 && P.clipY0_ == 2 && P.clipX1_ == 5 && P.clipY1_ == 5, "scroller restores the caller's clip");
      CHECK(P.setPanels(2) && !P.scrolling() && !P.clipOn_, "setPanels stops scrollers and removes the clip");
      P.setFont(NULL);
    }

    // colour helpers
    uint32_t c; CHECK(P4Panel32::parseColor("#FF8000", &c) && c == 0xFF8000, "hex");
    CHECK(P4Panel32::parseColor("1,2,3", &c) && c == 0x010203, "rgb");
    CHECK(P4Panel32::parseColor("5", &c) && c == 0xFF00FF, "digit");
    CHECK(P4Panel32::parseColor("orange", &c) && c == 0xFF8000, "name");
    CHECK(!P4Panel32::parseColor("nope", &c) && !P4Panel32::parseColor("9", &c) && !P4Panel32::parseColor("#12", &c), "bad colours");
    CHECK(P4Panel32::colorHSV(0, 255, 255) == 0xFF0000 && P4Panel32::colorHSV(120, 255, 255) == 0x00FF00, "hsv");

    // console
    P4Console con(P, Serial);
    CHECK(P.setPanels(2), "re-panels"); emuConfig(&P, pp, pp.a, pp.c, 64);
    cmd(con, "pn 2; box blue");
    CHECK(P.fb_[0].b == 255 && P.fb_[31*64+63].b == 255 && P.W_ == 64, "console box");
    cmd(con, "ctr 12 white HI"); { int n = 0; for (int y = 12; y < 19; y++) for (int x = 26; x < 38; x++) n += P.fb_[y*64+x].r == 255; CHECK(P.fb_[0].b == 0 && n > 10, "console ctr"); }
    cmd(con, "bri 100; pl 6; gam 18; sz 2; txt 1 4 #FF8000 AB");
    CHECK(P.bri_ == 100 && P.planes_ == 6 && P.gamma10_ == 18, "console settings");
    checkAll(P, "console state");
    cmd(con, "bogus"); cmd(con, "txt 1 4"); cmd(con, "p 1 1 notacolor"); cmd(con, "cfg A A"); cmd(con, "help");
    cmd(con, "cfg A C"); cmd(con, "scr 12 #FFFF00 40 SCROLL"); CHECK(P.scrolling(), "console scr"); cmd(con, "stop");
    cmd(con, "fx; fy; fx; fy"); CHECK(!P.flipX_ && !P.flipY_, "console flips");
  }

  // ---------- custom pins (data pins < 32, clk in bank 0 and bank 1) ----------
  {
    P4Pins cp = {10, 11, 12, 13, 14, 21, 17, 9, 1, 47, 46, 38, 45};
    P4Panel32 P; CHECK(P.begin(1, &cp), "begin custom");
    emuConfig(&P, cp, cp.a, cp.c, 32);
    P.fillRect(0, 0, 8, 8, 0x336699); P.drawPixel(31, 31, 0xFF0000); P.drawPixel(16, 16, 0x00FF00);
    checkAll(P, "custom pins (clk bank1)");
    P4Pins c2 = {10, 11, 12, 13, 14, 21, 17, 9, 1, 47, 46, 38, 25};   // clk in bank 0
    P4Panel32 Q; CHECK(Q.begin(1, &c2), "begin custom2");
    emuConfig(&Q, c2, c2.a, c2.c, 32);
    Q.fillRect(3, 3, 9, 9, 0xFF8000); Q.drawPixel(31, 0, 0x0000FF);
    checkAll(Q, "custom pins (clk bank0)");
    // row lines swapped via setRowLines (B = DCLK, D = SDIN)
    CHECK(Q.setRowLines(1, 3), "setRowLines"); emuConfig(&Q, c2, c2.b, c2.d, 32);
    checkAll(Q, "row lines B/D");
    CHECK(!Q.setRowLines(2, 2) && !Q.setRowLines(4, 0), "setRowLines invalid");
  }
  // ---------- invalid pins / limits ----------
  {
    P4Pins bad = P4DefaultPins(); bad.g1 = 40;
    P4Panel32 P; CHECK(!P.begin(1, &bad), "data pin >= 32 rejected");
    P4Panel32 Q; CHECK(Q.begin(100), "begin clamps panels"); CHECK(Q.panels() == P4Panel32::MAX_PANELS, "clamped to MAX_PANELS");
    Q.end(); CHECK(Q.width() == 0, "end");
    Q.refresh(); Q.show();   // must not crash after end()
  }
  printf(fails ? "\nSOME TESTS FAILED (%d)\n" : "\nALL TESTS PASSED\n", fails);
  return fails ? 1 : 0;
}
