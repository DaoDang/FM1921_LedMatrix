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
    for (int i = 0; i < 300; i++) { g_us += 41000; P.update(); if (P.scrX_ < seenMin) seenMin = P.scrX_; }
    CHECK(seenMin < 0 && P.scrX_ <= 64, "scroll moved & wrapped"); P.stopScroll(); CHECK(!P.scrolling(), "stop");

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
