#include "P4Console.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>

static char* tok(char** p) {
  char* s = *p;
  while (*s == ' ') s++;
  if (!*s) { *p = s; return NULL; }
  char* t = s;
  while (*s && *s != ' ') s++;
  if (*s) *s++ = 0;
  *p = s;
  return t;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

P4Console::P4Console(P4Panel32& panel, Stream& io)
  : p_(panel), io_(io), defColor_(0xFF0000), txtSize_(1) {}

void P4Console::printState() {
  io_.printf("[P4] module=%d W=%d | bit-mau=%d gamma=%.1f bri=%d unit=%dus sp=%d | cfg dclk=%c sdin=%c mk=%d inv=%d cl=%d fx=%d fy=%d sz=%d\n",
             p_.panels(), p_.width(), p_.colorDepth(), p_.gamma(), p_.brightness(), (int)p_.unitTime(), p_.clockDelay(),
             'A' + p_.dclkLine(), 'A' + p_.sdinLine(), p_.markerStep(), (int)p_.invertRowData(), p_.chainLength(),
             (int)p_.flipX(), (int)p_.flipY(), txtSize_);
}

void P4Console::printHelp() {
  io_.println("--- lenh (khong phan biet hoa/thuong, nhieu lenh cach nhau ';') ---");
  io_.println("mau c: 0..7 | #RRGGBB | r,g,b | RED GREEN BLUE YELLOW CYAN MAGENTA WHITE ORANGE PINK PURPLE");
  io_.println("p x y [c] | + x y [c] | hl y [c] | vl x [c] | dg [c] | box [c] | full [c] | clr");
  io_.println("rect x y w h [c] | line x0 y0 x1 y1 [c] | grad");
  io_.println("txt x y c chu | atxt x y c chu | ctr y c chu | scr y c ms chu | stop | sz n");
  io_.println("col c | bri 0..255 | gam x10 | pl 1..8 | un us | sp n | pn N");
  io_.println("cfg D S | mk n | inv 0/1 | cl n | fx | fy | ? | help");
}

void P4Console::begin() {
  io_.println();
  io_.println("=== P4Panel32 console ===  (go 'help' de xem lenh)");
  printState();
}

bool P4Console::execute(char* line) {
  char* p = line;
  char* cmd = tok(&p);
  if (!cmd) return true;
  bool miss = false;
  auto I = [&]() -> int { char* t = tok(&p); if (!t) { miss = true; return 0; } return atoi(t); };
  auto C = [&]() -> uint32_t {
    char* t = tok(&p); uint32_t c = defColor_;
    if (t && !P4Panel32::parseColor(t, &c)) io_.printf("[MAU KHONG HOP LE] '%s'\n", t);
    return c;
  };
  auto CR = [&]() -> uint32_t {            // mau bat buoc
    char* t = tok(&p); uint32_t c = defColor_;
    if (!t) { miss = true; return c; }
    if (!P4Panel32::parseColor(t, &c)) { io_.printf("[MAU KHONG HOP LE] '%s'\n", t); miss = true; }
    return c;
  };
  auto REST = [&]() -> char* { while (*p == ' ') p++; return p; };
#define IS(s) (strcasecmp(cmd, (s)) == 0)

  const int W = p_.width();

  if (IS("P"))        { int x = I(), y = I(); uint32_t c = C(); if (miss) return false; p_.stopScroll(); p_.clear(); p_.drawPixel(x, y, c); }
  else if (IS("+"))   { int x = I(), y = I(); uint32_t c = C(); if (miss) return false; p_.stopScroll(); p_.drawPixel(x, y, c); }
  else if (IS("HL"))  { int y = I(); uint32_t c = C(); if (miss) return false; p_.stopScroll(); p_.clear(); p_.drawHLine(0, y, W, c); }
  else if (IS("VL"))  { int x = I(); uint32_t c = C(); if (miss) return false; p_.stopScroll(); p_.clear(); p_.drawVLine(x, 0, P4Panel32::PANEL_H, c); }
  else if (IS("DG"))  { uint32_t c = C(); p_.stopScroll(); p_.clear(); for (int i = 0; i < 32; i++) p_.drawPixel(i, i, c); }
  else if (IS("BOX")) { uint32_t c = C(); p_.stopScroll(); p_.clear(); p_.drawRect(0, 0, W, P4Panel32::PANEL_H, c); }
  else if (IS("FULL")){ uint32_t c = C(); p_.stopScroll(); p_.fill(c); }
  else if (IS("CLR")) { p_.stopScroll(); p_.clear(); }
  else if (IS("RECT")){ int x = I(), y = I(), w = I(), h = I(); uint32_t c = C(); if (miss) return false; p_.stopScroll(); p_.fillRect(x, y, w, h, c); }
  else if (IS("LINE")){ int x0 = I(), y0 = I(), x1 = I(), y1 = I(); uint32_t c = C(); if (miss) return false; p_.stopScroll(); p_.drawLine(x0, y0, x1, y1, c); }
  else if (IS("GRAD")){ p_.stopScroll(); p_.drawColorTest(); }
  else if (IS("TXT") || IS("ATXT")) {
    bool add = (cmd[0] == 'a' || cmd[0] == 'A');
    int x = I(), y = I(); uint32_t c = CR(); char* s = REST();
    if (miss || !*s) return false;
    p_.stopScroll(); if (!add) p_.clear(); p_.drawText(x, y, s, c, txtSize_);
  }
  else if (IS("CTR")) {
    int y = I(); uint32_t c = CR(); char* s = REST();
    if (miss || !*s) return false;
    p_.stopScroll(); p_.clear(); p_.drawTextCentered(y, s, c, txtSize_);
  }
  else if (IS("SCR")) {
    int y = I(); uint32_t c = CR(); int ms = I(); char* s = REST();
    if (miss || !*s) return false;
    p_.startScroll(s, y, c, (uint16_t)clampi(ms, 5, 2000), txtSize_);
  }
  else if (IS("STOP")){ p_.stopScroll(); }
  else if (IS("SZ"))  { int n = I(); if (miss) return false; txtSize_ = clampi(n, 1, 4); }
  else if (IS("COL")) { uint32_t c = CR(); if (miss) return false; defColor_ = c; }
  else if (IS("BRI")) { int n = I(); if (miss) return false; p_.setBrightness((uint8_t)clampi(n, 0, 255)); }
  else if (IS("GAM")) { int n = I(); if (miss) return false; p_.setGamma(clampi(n, 10, 40) / 10.0f); }
  else if (IS("PL"))  { int n = I(); if (miss) return false; p_.setColorDepth((uint8_t)clampi(n, 1, P4Panel32::MAX_PLANES)); }
  else if (IS("UN"))  { int n = I(); if (miss) return false; p_.setUnitTime((uint16_t)clampi(n, 1, 50)); }
  else if (IS("SP"))  { int n = I(); if (miss) return false; p_.setClockDelay((uint8_t)clampi(n, 0, 40)); }
  else if (IS("PN"))  { int n = I(); if (miss) return false; if (!p_.setPanels((uint8_t)clampi(n, 1, P4Panel32::MAX_PANELS))) { io_.println("[HET RAM]"); return false; } }
  else if (IS("CFG")) {
    char* a = tok(&p); char* b = tok(&p);
    if (!a || !b || strlen(a) != 1 || strlen(b) != 1) return false;
    int da = toupper(a[0]) - 'A', sb = toupper(b[0]) - 'A';
    if (da < 0 || da > 3 || sb < 0 || sb > 3) return false;
    if (!p_.setRowLines((uint8_t)da, (uint8_t)sb)) return false;
  }
  else if (IS("MK"))  { int n = I(); if (miss) return false; p_.setMarkerStep((uint8_t)(((n % 16) + 16) % 16)); }
  else if (IS("INV")) { int n = I(); if (miss) return false; p_.setInvertRowData(n != 0); }
  else if (IS("CL"))  { int n = I(); if (miss) return false; p_.setChainLength((uint8_t)clampi(n, 16, 64)); }
  else if (IS("FX"))  { p_.setFlipX(!p_.flipX()); }
  else if (IS("FY"))  { p_.setFlipY(!p_.flipY()); }
  else if (IS("HELP")){ printHelp(); }
  else if (IS("?"))   { }
  else return false;
#undef IS
  return true;
}

void P4Console::handle() {
  if (!io_.available()) return;
  String str = io_.readStringUntil('\n');
  str.trim();
  if (str.length() == 0) return;
  char buf[200];
  str.toCharArray(buf, sizeof(buf));
  char* save = NULL;
  for (char* t = strtok_r(buf, ";", &save); t; t = strtok_r(NULL, ";", &save)) {
    char tmp[200];
    strncpy(tmp, t, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
    if (!execute(tmp)) io_.printf("[KHONG HIEU LENH] '%s'\n", t);
  }
  printState();
}
