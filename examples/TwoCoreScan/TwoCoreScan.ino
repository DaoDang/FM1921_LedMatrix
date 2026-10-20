// P4Panel32 - TwoCoreScan
// Cùng hiển thị như ví dụ vùng hiển thị (giờ tĩnh ở trên, chữ chạy ở dưới) nhưng KHÔNG nháy:
// việc quét LED chạy một mình ở lõi 1 và không bao giờ bị chặn; việc vẽ + đổi ảnh sang bit-plane
// (mỗi bước chữ chạy mất ~1 ms) chạy ở lõi 0 vào bộ đệm thứ hai, quét xong khung nào thì đổi con trỏ.
//
// Cần ESP32 hai lõi (ESP32, ESP32-S3). Mọi lệnh vẽ, startScrollIn() và prepare() phải gọi từ MỘT task (task vẽ).
// begin() và các hàm set...() gọi trong setup(), trước khi tạo task.

#include <P4Panel32.h>
#include <DejaVuVN_9.h>
#include <DejaVuVN_16.h>

#define NUM_PANELS 2

P4Panel32 panel;

static void scanTask(void*) {                      // lõi 1: chỉ quét
  uint32_t lastYield = millis();
  for (;;) {
    panel.swapIfReady();                           // nhận ảnh mới (chỉ đổi con trỏ)
    panel.refresh();                               // quét 1 khung
    if (millis() - lastYield >= 2000) { lastYield = millis(); vTaskDelay(1); }   // nhường lõi 1 một tick mỗi 2 s
  }
}

static void drawTask(void*) {                      // lõi 0: vẽ + chuẩn bị ảnh
  panel.setFont(&DejaVuVN_16);
  panel.setClip(0, 0, 64, 17);
  panel.clearRect(0, 0, 64, 17);
  panel.drawText(12, -1, "17:30", P4_YELLOW);
  panel.clearClip();

  panel.setFont(&DejaVuVN_9);
  panel.startScrollIn(0, 17, 64, 13, "DL 17-10-2026", P4_CYAN, 40);

  for (;;) {
    panel.prepare();                               // chữ chạy 1 bước + đổi ảnh (khi có thay đổi)
    vTaskDelay(1);
  }
}

void setup() {
  panel.begin(NUM_PANELS);
  panel.setBrightness(180);
  xTaskCreatePinnedToCore(scanTask, "scan", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(drawTask, "draw", 4096, NULL, 1, NULL, 0);
}

void loop() { vTaskDelay(1000); }
