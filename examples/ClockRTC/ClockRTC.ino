// P4Panel32 - ClockRTC
// Digital clock on a 2-module (64 x 32) P4 panel, time from a real-time clock (RTC) module on I2C.
//
//   top line    : HH:MM  - big (16 px), static, centred; the ':' blinks once per second
//   bottom line : "DL 17-10-2026" - small (9 px), scrolling (DL = Dương lịch)
//
// RTC wiring: SDA = GPIO10, SCL = GPIO12 (change RTC_SDA / RTC_SCL below), plus 3.3 V and GND.
// Power the RTC module from 3.3 V: the pull-up resistors on most modules go to VCC and the
// ESP32 pins are not 5 V tolerant.
// Chips (found automatically): DS3231 and DS1307 (I2C address 0x68), PCF8563 / BM8563 (0x51).
//
// Setting the time:
//   * An RTC that holds no valid time (new chip, battery was removed) is set to the time this
//     sketch was COMPILED - upload right after building.
//   * Serial monitor (115200, send with Enter):   SET 2026-10-17 08:30:00
//   * or set SET_FROM_COMPILE_TIME to 1, upload once, set it back to 0 and upload again.
//
// The two lines are separate display areas: the time is drawn inside a clip rectangle and the date
// scrolls with startScrollIn(), which only redraws its own rectangle - so neither disturbs the other.
//
// PlatformIO: paste into src/main.cpp and add  #include <Arduino.h>  as the first line.
// Save the file as UTF-8.

#include <Wire.h>
#include <strings.h>
#include <P4Panel32.h>
#include <DejaVuVN_9.h>
#include <DejaVuVN_16.h>

#define NUM_PANELS 2                  // 2 modules = 64 x 32
#define RTC_SDA    10
#define RTC_SCL    12
#define BRIGHTNESS 200
#define SET_FROM_COMPILE_TIME 0       // 1 = set the RTC to the build time at every boot

static const uint32_t TIME_COLOR     = P4_YELLOW;
static const uint32_t DATE_COLOR     = P4_CYAN;
static const uint16_t DATE_SCROLL_MS = 40;      // ms per pixel

// Display areas (panel rows). The 16 px digits are 12 rows high; TIME_TEXT_Y is the top of their
// 21-row line cell (negative = the empty rows above the digits are off the panel).
static const int TIME_Y      = 0;
static const int TIME_H      = 17;              // area rows 0..16
static const int TIME_TEXT_Y = -1;              // digits on rows 4..15
static const int DATE_Y      = 17;
static const int DATE_H      = 13;              // area rows 17..29 = the 13-row line cell of the 9 px font

struct DateTime { int year, month, day, hour, minute, second; };

// ---------------------------------------------------------------- RTC (DS3231 / DS1307 / PCF8563)
static uint8_t rtcAddr = 0;                     // 0 = not found

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 15)); }
static uint8_t bin2bcd(int v)     { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static bool    isPcf()            { return rtcAddr == 0x51; }

static bool i2cPing(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }

static int dayOfWeek(int y, int m, int d) {     // 0 = Sunday
  static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static bool validTime(const DateTime& t) {
  return t.year >= 2000 && t.year <= 2099 && t.month >= 1 && t.month <= 12 && t.day >= 1 && t.day <= 31 &&
         t.hour >= 0 && t.hour <= 23 && t.minute >= 0 && t.minute <= 59 && t.second >= 0 && t.second <= 59;
}

// *stopped = the chip itself says the time is not valid (DS1307 halted / PCF8563 voltage low).
static bool rtcRead(DateTime& t, bool* stopped = NULL) {
  const bool pcf = isPcf();
  Wire.beginTransmission(rtcAddr);
  Wire.write((uint8_t)(pcf ? 0x02 : 0x00));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(rtcAddr, (uint8_t)7) != 7) return false;
  uint8_t r[7];
  for (int i = 0; i < 7; i++) r[i] = (uint8_t)Wire.read();
  if (stopped) *stopped = (r[0] & 0x80) != 0;
  t.second = bcd2bin(r[0] & 0x7F);
  t.minute = bcd2bin(r[1] & 0x7F);
  if (!pcf && (r[2] & 0x40)) t.hour = bcd2bin(r[2] & 0x1F) % 12 + ((r[2] & 0x20) ? 12 : 0);   // 12 h mode
  else                       t.hour = bcd2bin(r[2] & 0x3F);
  t.day   = bcd2bin((pcf ? r[3] : r[4]) & 0x3F);   // DS: r[3] = weekday, r[4] = date; PCF: r[3] = date
  t.month = bcd2bin(r[5] & 0x1F);
  t.year  = 2000 + bcd2bin(r[6]);
  return true;
}

static bool rtcWrite(const DateTime& t) {
  const bool pcf = isPcf();
  const int dow = dayOfWeek(t.year, t.month, t.day);
  Wire.beginTransmission(rtcAddr);
  Wire.write((uint8_t)(pcf ? 0x02 : 0x00));
  Wire.write(bin2bcd(t.second));               // bit 7 = 0: starts a halted DS1307, clears PCF8563 "voltage low"
  Wire.write(bin2bcd(t.minute));
  Wire.write(bin2bcd(t.hour));                 // 24 h mode
  if (pcf) { Wire.write(bin2bcd(t.day)); Wire.write((uint8_t)dow); }
  else     { Wire.write((uint8_t)(dow + 1)); Wire.write(bin2bcd(t.day)); }
  Wire.write(bin2bcd(t.month));
  Wire.write(bin2bcd(t.year % 100));
  return Wire.endTransmission() == 0;
}

static bool compileTime(DateTime& t) {         // __DATE__ = "Oct 17 2026", __TIME__ = "08:30:00"
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char m[8] = {0};
  int d, y, hh, mm, ss;
  if (sscanf(__DATE__, "%3s %d %d", m, &d, &y) != 3) return false;
  if (sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss) != 3) return false;
  const char* p = strstr(months, m);
  if (!p) return false;
  t.year = y; t.month = (int)(p - months) / 3 + 1; t.day = d; t.hour = hh; t.minute = mm; t.second = ss;
  return true;
}

// Finds the chip; if it holds no valid time, sets it to the build time.
static bool rtcBegin() {
  rtcAddr = 0;
  if (i2cPing(0x68)) rtcAddr = 0x68;
  else if (i2cPing(0x51)) rtcAddr = 0x51;
  else return false;
  DateTime t;
  bool stopped = false;
  const bool ok = rtcRead(t, &stopped);
  if (SET_FROM_COMPILE_TIME || !ok || stopped || !validTime(t)) {
    DateTime b;
    if (compileTime(b) && rtcWrite(b)) Serial.println("RTC: set to the build time");
  }
  if (rtcRead(t)) Serial.printf("RTC 0x%02X: %04d-%02d-%02d %02d:%02d:%02d\n", rtcAddr, t.year, t.month, t.day, t.hour, t.minute, t.second);
  return true;
}

// ---------------------------------------------------------------- display
P4Panel32 panel;

// HH:MM centred in the top area. The colon is drawn black when "off", so it keeps its space and the
// digits never move (the digits of the font all have the same width).
static void drawTime(int hour, int minute, bool colon) {
  char hh[8], mm[8];
  snprintf(hh, sizeof hh, "%02d", hour);
  snprintf(mm, sizeof mm, "%02d", minute);
  panel.setFont(&DejaVuVN_16);
  panel.setClip(0, TIME_Y, panel.width(), TIME_H);             // nothing can leak into the date area
  panel.clearRect(0, TIME_Y, panel.width(), TIME_H);
  int x = (panel.width() - panel.textWidth("00:00")) / 2;
  x = panel.drawText(x, TIME_TEXT_Y, hh, TIME_COLOR);          // drawText returns the x after the text
  x = panel.drawText(x, TIME_TEXT_Y, ":", colon ? TIME_COLOR : P4_BLACK);
  panel.drawText(x, TIME_TEXT_Y, mm, TIME_COLOR);
  panel.clearClip();
}

// "DL dd-mm-yyyy" scrolling in the bottom area; calling it again (new day) replaces the text.
static void startDateScroll(const DateTime& t) {
  char text[40];
  snprintf(text, sizeof text, "DL %02d-%02d-%04d", t.day, t.month, t.year);
  panel.setFont(&DejaVuVN_9);
  panel.startScrollIn(0, DATE_Y, panel.width(), DATE_H, text, DATE_COLOR, DATE_SCROLL_MS);
}

static void showNoRtc() {
  panel.stopScroll();
  panel.clear();
  panel.setFont(&DejaVuVN_9);
  panel.drawTextCentered((32 - panel.fontHeight()) / 2, "Lỗi RTC", P4_RED);
}

// ---------------------------------------------------------------- serial command: SET 2026-10-17 08:30:00
static void handleSerial() {
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  char buf[48];
  line.toCharArray(buf, sizeof(buf));
  DateTime t;
  if (strncasecmp(buf, "SET ", 4) == 0 &&
      sscanf(buf + 4, "%d-%d-%d %d:%d:%d", &t.year, &t.month, &t.day, &t.hour, &t.minute, &t.second) == 6 && validTime(t)) {
    Serial.println(rtcAddr && rtcWrite(t) ? "RTC set" : "RTC write failed");
  } else {
    Serial.println("Usage: SET 2026-10-17 08:30:00");
  }
}

// ---------------------------------------------------------------- main
static bool          rtcReady = false, haveTime = false;
static DateTime      cur = {0, 0, 0, 0, 0, 0};
static int           lastSecond = -1;
static unsigned long secStart = 0, lastRead = 0, lastScan = 0;
static uint8_t       failCount = 0;
static int           shownHour, shownMinute, shownColon, shownDay, shownMonth, shownYear;

static void forgetShown() {
  haveTime = false; lastSecond = -1; failCount = 0;
  shownHour = shownMinute = shownColon = shownDay = shownMonth = shownYear = -1;
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  Wire.begin(RTC_SDA, RTC_SCL);
  if (!panel.begin(NUM_PANELS)) {
    Serial.println("panel.begin() failed");
    while (true) delay(1000);
  }
  panel.setBrightness(BRIGHTNESS);
  forgetShown();
  rtcReady = rtcBegin();
  if (!rtcReady) { Serial.println("RTC not found (check SDA/SCL wiring)"); showNoRtc(); }
}

void loop() {
  const unsigned long ms = millis();

  if (Serial.available()) handleSerial();

  if (!rtcReady) {
    if (ms - lastScan >= 2000) {                                // look for the RTC again every 2 s
      lastScan = ms;
      if (rtcBegin()) { rtcReady = true; panel.clear(); forgetShown(); }
    }
  } else if (ms - lastRead >= 100) {                            // read the RTC 10 times per second
    lastRead = ms;
    DateTime t;
    if (rtcRead(t) && validTime(t)) {
      failCount = 0;
      if (t.second != lastSecond) { lastSecond = t.second; secStart = ms; }   // colon phase follows the RTC second
      cur = t; haveTime = true;
    } else if (++failCount >= 30) {                             // no answer for 3 s
      rtcReady = false; forgetShown(); showNoRtc();
    }
  }

  if (rtcReady && haveTime) {
    const int colon = (ms - secStart < 500) ? 1 : 0;            // ':' visible during the first half of each second
    if (cur.hour != shownHour || cur.minute != shownMinute || colon != shownColon) {
      drawTime(cur.hour, cur.minute, colon != 0);
      shownHour = cur.hour; shownMinute = cur.minute; shownColon = colon;
    }
    if (cur.day != shownDay || cur.month != shownMonth || cur.year != shownYear) {
      startDateScroll(cur);
      shownDay = cur.day; shownMonth = cur.month; shownYear = cur.year;
    }
  }

  panel.show();
}
