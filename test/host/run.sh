#!/bin/sh
# Builds and runs the host tests with the ArduinoJson that PlatformIO installed (run `pio run` once first).
set -e
cd "$(dirname "$0")/../.."
out="${TMPDIR:-/tmp}/metadisplay-host-test"
g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined -DTEST_DIR="\"$PWD/test/host\"" \
  -Iinclude -I.pio/libdeps/esp32c6/ArduinoJson/src test/host/test_host.cpp src/Dynamic.cpp -o "$out"
"$out"
