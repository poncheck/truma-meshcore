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
echo "---"
g++ -std=c++17 -Wall -Wextra -o /tmp/test_trumahandshake test/test_trumahandshake.cpp trumahandshake.cpp trumalin.cpp trumastatus.cpp trumaconv.cpp
/tmp/test_trumahandshake
echo "---"
g++ -std=c++17 -Wall -Wextra -o /tmp/test_trumalog test/test_trumalog.cpp trumalog.cpp
/tmp/test_trumalog
echo "---"
g++ -std=c++17 -Wall -Wextra -o /tmp/test_trumaframereader test/test_trumaframereader.cpp trumaframereader.cpp trumalin.cpp trumaconv.cpp
/tmp/test_trumaframereader
echo "---"
g++ -std=c++17 -Wall -Wextra -o /tmp/test_trumalinwriter test/test_trumalinwriter.cpp trumalinwriter.cpp trumalin.cpp trumaconv.cpp
/tmp/test_trumalinwriter
echo "---"
g++ -std=c++17 -Wall -Wextra -o /tmp/test_trumaanswerwindow test/test_trumaanswerwindow.cpp
/tmp/test_trumaanswerwindow
