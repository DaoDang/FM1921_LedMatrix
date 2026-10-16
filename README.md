# P4Panel32

Arduino / PlatformIO library for **P4 32x32 RGB LED panels** (PCB label like `T-T4-1R1G1B-FM1921-32X32-16S2-02`) on **ESP32**.
Thư viện Arduino / PlatformIO điều khiển **bảng LED P4 32x32** bằng **ESP32**.

- Chain **N modules in series** (1 … 16) / ghép nối tiếp N module
- 24-bit RGB colour (BCM bit planes + gamma), **brightness** control / màu RGB, chỉnh độ sáng
- Points, lines, rectangles, **5x7 text**, centred text, **scrolling text** / vẽ điểm, đường, hình chữ nhật, chữ, chữ chạy
- **Vietnamese text (UTF-8)** with DejaVu bitmap fonts of 9 / 12 / 16 px, any size can be generated / **chữ tiếng Việt có dấu (UTF-8)** với font DejaVu 9 / 12 / 16 px, tạo thêm cỡ khác được
- Optional **serial command console** for testing / console lệnh qua Serial để thử nghiệm

> ⚠️ **Status / Tình trạng.** Tested on an **ESP32-S3** with **1 and 2 modules** only.
> The row-select model (A = DCLK, C = SDIN, D unused) was *fitted to measurements* of one panel; the
> driver IC is under potting glue, so it is **not datasheet-verified**. If your panel behaves differently,
> use `setRowLines()`, `setMarkerStep()`, `setInvertRowData()`, `setChainLength()` (see below).
> Other ESP32 variants and more than 2 modules are untested.
> Đã thử trên **ESP32-S3** với **1 và 2 module**. Mô hình quét hàng được *suy ra từ phép đo* trên một bảng,
> chưa kiểm chứng bằng datasheet.

## Install / Cài đặt

**Arduino IDE** – *Sketch → Include Library → Add .ZIP Library…* (download the repo as ZIP), or copy the folder to `Documents/Arduino/libraries/P4Panel32`.

**PlatformIO** – in `platformio.ini`:

```ini
[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
lib_deps = https://github.com/daodang/FM1921_LedMatrix.git
; for the serial console example:
monitor_filters = send_on_enter
monitor_echo = yes
```

or copy the folder into your project's `lib/P4Panel32/`. If the build runs out of memory on Windows: `pio run -j 1`.

## Wiring / Đấu dây (default pins)

| Panel | R1 | G1 | B1 | R2 | G2 | B2 | A | B | C | D | LAT | OE | CLK |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ESP32-S3 GPIO | 4 | 5 | 6 | 7 | 15 | 16 | 18 | 8 | 3 | 42 | 40 | 2 | 41 |

Colour data pins (R1…B2) **must be GPIO 0–31**. Other pins can be changed with a `P4Pins` struct:

```cpp
P4Pins pins = {4,5,6,7,15,16,  18,8,3,42,  40,2,41};   // r1 g1 b1 r2 g2 b2, a b c d, lat oe clk
panel.begin(1, &pins);
```

Chain: ESP32 → **IN** of module 1 (right side seen from the front); **OUT** of module 1 → IN of module 2 (its left side) …
Coordinates: `x = 0` is the left edge of the *last* module (farthest from the ESP32), `x = 32·N − 1` is the right edge of module 1, `y = 0` is the top row.
Power each module from a proper 5 V supply (a 32x32 panel can draw several amps at full white).

## Quick start

```cpp
#include <P4Panel32.h>

P4Panel32 panel;

void setup() {
  panel.begin(2);                       // 2 modules in series -> 64 x 32
  panel.setBrightness(180);             // 0..255
  panel.drawRect(0, 0, panel.width(), panel.height(), P4_BLUE);
  panel.drawLine(0, 0, 63, 31, P4Panel32::color(255, 128, 0));
  panel.drawTextCentered(12, "HELLO", P4_WHITE);
}

void loop() {
  panel.show();                         // call continuously: converts if changed + scans one frame
}
```

Important: the panel must be **refreshed continuously** (`show()` every `loop()`), do not use long `delay()` calls.

## API summary

| Group | Functions |
|---|---|
| Life cycle | `begin(panels, pins)`, `end()`, `setPanels(n)`, `width()`, `height()`, `panels()` |
| Running | `show()` (= `update()` + `refresh()`), `update()`, `refresh()`, `commit()` |
| Drawing | `clear()`, `fill(c)`, `drawPixel(x,y,c)`, `drawHLine`, `drawVLine`, `drawLine`, `drawRect`, `fillRect`, `drawColorTest()` |
| Text | `drawChar`, `drawGlyph`, `drawText(x,y,s,c,size)`, `drawTextCentered(y,s,c,size)`, `textWidth`, `startScroll(text,y,c,ms,size)`, `stopScroll()` |
| Fonts | `setFont(&DejaVuVN_12)` / `setFont(NULL)`, `font()`, `fontHeight()`, `fontAscent()` |
| Colour | `color(r,g,b)`, `colorHSV(h,s,v)`, `parseColor("#FF8000")`, constants `P4_RED` … |
| Appearance | `setBrightness(0..255)`, `setGamma(2.2)`, `setColorDepth(1..8)`, `setUnitTime(us)`, `setClockDelay(nops)`, `setFlipX/Y` |
| Scan model | `setRowLines(dclk, sdin)` (0=A … 3=D), `setMarkerStep(0..15)`, `setInvertRowData(bool)`, `setChainLength(16..64)` |

Colours are `0xRRGGBB`. Text is UTF-8. The default font is the built-in 5x7 (ASCII only, 6 px per character × size; other characters show `?`); call `setFont()` for Vietnamese (next section).

Tuning: `setColorDepth()` higher = smoother colours but lower refresh rate; `setUnitTime()` higher = brighter but slower; with many modules, flicker can be reduced with a lower colour depth. Brightness is time based (`OE` on-time).

## Vietnamese text / Chữ tiếng Việt

Strings are **UTF-8**. Save the sketch as UTF-8 (default in Arduino IDE and VS Code) and type accents precomposed (normal Unikey / Telex output).
Include the font(s) you want and select them with `setFont()`:

```cpp
#include <P4Panel32.h>
#include <DejaVuVN_9.h>
#include <DejaVuVN_16.h>

P4Panel32 panel;

void setup() {
  panel.begin(2);                                              // 64 x 32
  panel.setFont(&DejaVuVN_16);                                 // 16 px line on top
  panel.drawTextCentered(0, "Hà Nội", P4_YELLOW);
  panel.setFont(&DejaVuVN_9);                                  // 9 px line at the bottom
  panel.drawTextCentered(32 - panel.fontHeight(), "Việt Nam", P4_CYAN);
}
void loop() { panel.show(); }
```

| Font | em size | line cell (`fontHeight()`) | ordinary text rows | flash (total) |
|---|---|---|---|---|
| `DejaVuVN_9`  |  9 px | 13 rows | ~12 (lowercase with marks 10 + 2 below) | ~3.7 KB |
| `DejaVuVN_12` | 12 px | 17 rows | ~15 (12 + 3) | ~4.4 KB |
| `DejaVuVN_16` | 16 px | 21 rows | ~17 (14 + 3) | ~5.8 KB |

- `y` in `drawText()` is the **top of the line cell**; the baseline is at `y + fontAscent()`. The cell is as tall as the
  tallest capital with stacked marks (Ấ, Ẫ ...), so ordinary text uses fewer rows (table). A 16 px line at `y = 0` and a 9 px line at
  `y = 32 - fontHeight()` therefore do not touch.
- `size` (1, 2, ...) still works and scales the font by whole pixels. `startScroll()` remembers the font that is set when it is called.
- Each font holds ASCII, all 134 Vietnamese letters, and `° ± × ÷ · – — ‘ ’ “ ” … • ₫ € £ ©`. A character the font lacks shows `?`.
  Invalid UTF-8 shows `?` too. Fonts only cost flash if you include them.
- Text is not wrapped: a 64 px panel fits only a few characters of the 16 px font ("Hà Nội" = 51 px). Use `textWidth()` and `startScroll()`.
- Combining accents (decomposed / NFD text) are not supported - use precomposed letters.

**Other sizes or another font / cỡ khác, font khác:** `extras/fontgen/make_vn_font.py` generates the headers from any TTF:

```
pip install freetype-py matplotlib pillow
python extras/fontgen/make_vn_font.py --sizes 10 14 --out src                 # DejaVuVN_10.h, DejaVuVN_14.h
python extras/fontgen/make_vn_font.py --sizes 16 --ttf DejaVuSans-Bold.ttf --name DejaVuBoldVN --out src
python extras/fontgen/make_vn_font.py --sizes 12 --preview preview.png --text "Xin chào"   # magnified preview
```

See `extras/fontgen/README.md`. DejaVu licence: [NOTICE](NOTICE).

### Serial console (optional)

`P4Console console(panel, Serial);` → `console.begin()` in `setup()`, `console.handle()` in `loop()`.
Type `help` in the Serial Monitor (115200, Newline). Example: `pn 2; box blue; ctr 12 white HI`, `scr 12 yellow 40 Hello world`, `bri 100; grad`.

## Self test without hardware

`extras/host-test/run.sh` (needs g++) runs the library against a software model of the panel and checks colours,
brightness, chaining, flips, custom pins, and the UTF-8 / font code (glyph tables, invalid input, scrolling, the two-line layout of the Vietnamese example).
It verifies internal consistency only – **not** real hardware behaviour.

## Examples

`BasicDrawing`, `TextAndScroll`, `VietnameseText`, `SerialConsole` (in `examples/`).

## License

DANG – see [LICENSE](LICENSE). Font tables: see [NOTICE](NOTICE) (5x7 font: Adafruit GFX, BSD; DejaVu fonts: Bitstream Vera licence, DejaVu changes public domain).
