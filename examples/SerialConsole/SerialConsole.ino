// P4Panel32 - SerialConsole
// Interactive testing from the Serial Monitor (115200, line ending: Newline).
// Type 'help' for the command list. Examples:
//   pn 2                    -> 2 modules in series
//   box blue; ctr 12 white HI
//   txt 1 4 #FF8000 Hello
//   scr 12 yellow 40 Scrolling text
//   bri 100 ; grad
// PlatformIO: add to platformio.ini ->  monitor_filters = send_on_enter
//                                       monitor_echo = yes

#include <P4Panel32.h>
#include <P4Console.h>

P4Panel32 panel;
P4Console console(panel, Serial);

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  if (!panel.begin(1)) {
    Serial.println("panel.begin() failed");
    while (true) delay(1000);
  }
  for (int i = 0; i < 32; i++) panel.drawPixel(i, i, P4_RED);      // default: red diagonal
  console.begin();
}

void loop() {
  console.handle();
  panel.show();
}
