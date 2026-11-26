#!/usr/bin/env bash
# Host-side test runner for the Truma<->MeshCore firmware port.
# No hardware, no ESP32 toolchain needed -- pure logic, runs anywhere with g++.
set -euo pipefail
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -Wextra -o /tmp/test_trumalin test/test_trumalin.cpp trumalin.cpp trumaconv.cpp
/tmp/test_trumalin
echo "---"
g++ -std=c++17 -Wall -Wextra -o /tmp/test_victronble test/test_victronble.cpp victronble.cpp victrondevices.cpp aes128.cpp
/tmp/test_victronble
echo "---"
g++ -std=c++17 -Wall -Wextra -o /tmp/test_trumastatus test/test_trumastatus.cpp trumastatus.cpp trumaconv.cpp
/tmp/test_trumastatus
