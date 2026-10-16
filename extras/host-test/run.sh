#!/bin/sh
# Host-side self test (no ESP32 needed): emulates the fitted panel model and checks that the
# brightness seen on a virtual panel matches the requested colours. Needs g++.
cd "$(dirname "$0")" || exit 1
L=../../src
g++ -std=gnu++11 -Wall -Wextra -Istub -I$L -o /tmp/p4_hosttest test.cpp $L/P4Panel32.cpp $L/P4Console.cpp && /tmp/p4_hosttest
