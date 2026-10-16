// P4Console - optional text command console for P4Panel32 (Serial Monitor testing).
// Usage:  P4Console console(panel, Serial);  console.begin();  ... console.handle(); in loop().
#ifndef P4CONSOLE_H
#define P4CONSOLE_H

#include <Arduino.h>
#include "P4Panel32.h"

class P4Console {
public:
  P4Console(P4Panel32& panel, Stream& io);

  void begin();                 // prints a short banner + state
  void handle();                // reads one line if available and executes it
  bool execute(char* line);     // execute one command (modifies the buffer)
  void printState();
  void printHelp();

private:
  P4Panel32& p_;
  Stream&    io_;
  uint32_t   defColor_;
  int        txtSize_;
};

#endif
