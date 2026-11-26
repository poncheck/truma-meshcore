# Truma Combi + Victron Energy ↔ MeshCore

Custom MeshCore firmware for a dedicated **LilyGO T3 V1.6.1** node
(ESP32 + SX1276, OLED display): Victron Energy BLE telemetry (SmartShunt +
SmartSolar) + Truma Combi LIN control, both reachable remotely over the
MeshCore LoRa mesh via the standard MeshCore phone app (encrypted admin
CLI, no extra app/server).

**Status: software port in progress, pin wiring confirmed on real
hardware, firmware integration NOT YET complete.** See "What's done /
what's not" below before wiring anything to a live heater.

> **Board history note:** this project originally targeted a XIAO nRF52
> (lower power draw). It was switched to the LilyGO T3 V1.6.1 (higher power
> draw, but has a built-in display to see status at a glance) — see git
> history for the nRF52/Bluefruit BLE scanning code if ever needed again.

## Wiring

### 1. Victron BLE (no wiring needed)

SmartShunt and SmartSolar are read passively over Bluetooth (BLE "Instant
Readout" advertisements) — no physical connection to them at all. The node
just needs their MAC address + AES encryption key (from the Victron Connect
app's "Instant Readout" setup for each device; see Victron's docs), set at
build time via firmware config.

### 2. Truma Combi LIN bus

**Confirmed bus layout (verified against a real CP Plus PCB):**
the CP Plus has exactly **one** bus connector, a single RJ12 socket
hand-labeled **"CI Bus"** on the PCB. There is no second/dedicated iNet
port. The heater itself has two RJ12 sockets, but — because LIN is a
multi-drop bus — those are almost certainly the same electrical bus
duplicated, not two independent buses. (Not every CP Plus revision is
guaranteed identical — check your own PCB before assuming this applies.)

**-> Installation point: splice a splitter into the existing CI Bus cable**
(the single cable between CP Plus and the heater). This is the officially
documented primary method in the `inetbox.py` reference project's hardware
instructions ("...or using a splitter module into the existing connection
between Combi and the control panel") — not a workaround.

```
[CP Plus] ---- existing CI Bus cable ---- [splitter] ---- [Combi heater]
                                               |
                                      [RJ12 cable, 6P6C, all 6 wires]
                                               |
                                      [LIN-to-UART transceiver module]
                                               |
                                 [LilyGO T3 V1.6.1 — GPIO4 (TX) / GPIO34 (RX)]
```

**Transceiver module pinout** (per `inetbox.py`'s hardware requirements,
adapted to the T3's confirmed-free UART pins):

| Transceiver pin | Connects to |
|---|---|
| **12V** | A 12V supply that also powers the Combi/CP Plus (NOT from the T3 itself) |
| **TX** (module output) | T3 **GPIO34** (input-only pin, confirmed free of LoRa/OLED/button) |
| **RX** (module input) | T3 **GPIO4** (confirmed free, verified via live UART loopback test) |
| **LIN** | RJ12 **pin 3** (verify with a multimeter against your specific module's datasheet — some docs list pin 3, some pin 4; don't guess) |
| **GND** | RJ12 **pin 5**, AND a second GND wire to the T3's GND |
| INH / SLP | not used |

**How GPIO4/GPIO34 were confirmed (not guessed):** this board's pinout had
conflicting info online (some "T3 V1.6.1" diagrams disagree with each
other), so pins were verified directly on this physical unit:
1. Flashed the stock MeshCore repeater build (`P_LORA_RESET=14`) — radio
   initialized, node generated an identity and transmitted a real
   advertisement, confirming GPIO14 is the true LoRa reset pin (and ruling
   out a conflicting online diagram that claimed GPIO23).
2. Flashed a minimal standalone test sketch configuring `Serial2` on
   RX=GPIO34/TX=GPIO4 and bridging them with a jumper wire — a test byte
   echoed back reliably, confirming both pins work as UART with zero
   interference from LoRa/OLED/the user button.

⚠️ **If your transceiver has an INH (inhibit) pin and it's the same model
referenced by `TruMinus`/the LIN-master approach: bridge LIN and INH
together ONLY if you were building a bus master.** We are explicitly
**not** doing that (see "Why slave, not master" below) — for this
iNet-Box-slave role, leave INH unconnected or tied per your module's
"always listening" wiring, not the master-mode wiring. If your module's
datasheet shows a jumper or solder pad for master/slave mode, set it to
slave/listen mode if present.

### 3. RJ12 cable/splitter type — the mistake to avoid

Must be **6P6C** (6-position, 6-conductor) — the kind with all 6 wires
connected end to end. Many cheap "telephone splitters" sold for landline
phones are 6P4C (only 4 of the 6 positions wired) and **will not work** —
pins 3 and 5 (LIN and GND) may simply not be present in the cable.

## Why slave, not master (do not use TruMinus-style firmware here)

Two different reference approaches exist for talking to a Truma Combi over
LIN:
- **`danielfett/inetbox.py`** — emulates a Truma **iNet Box**, a LIN
  **slave** device. The CP Plus stays the one bus master; this firmware
  just answers when addressed. **This is what we're porting.**
- **`olivluca/TruMinus`** — emulates the **CP Plus itself**, acting as the
  LIN **master**. Only safe if nothing else is already mastering the bus.

If a live CP Plus is already mastering the bus, a TruMinus-style master-mode
firmware wired onto the same bus would **contend** with it — this is the
suspected cause of random Combi shutdowns / error codes reported by people
who wired master-mode firmware onto a bus with a live CP Plus still attached
(see WomoLIN GitHub issue discussions). The slave approach only ever replies
when spoken to, so it's safe to splice into the shared CI Bus cable
alongside a working CP Plus.

## What's done / what's not (as of 2026-11-26)

✅ **Done, host-tested (89/89 checks passing, `firmware/run_tests.sh`, no hardware needed):**
- LIN transport-layer primitives (checksum, PID parity, frame parsing) — `trumalin.{hpp,cpp}`
- Value conversions (temperature codes, heating mode, water target, error codes) — `trumaconv.{hpp,cpp}`
- Truma main status buffer parse/serialize (cmd 0x33) — `trumastatus.{hpp,cpp}`
- AES-128 (from scratch, no external crypto lib) — `aes128.{hpp,cpp}`
- Victron BLE advertisement decrypt (AES-128-CTR) — `victronble.{hpp,cpp}`
- SmartShunt/SmartSolar bitfield parsers — `victrondevices.{hpp,cpp}`

✅ **Confirmed on real hardware (LilyGO T3 V1.6.1, physical unit):**
- Stock MeshCore `LilyGo_TLora_V2_1_1_6_repeater` build flashes, boots,
  initializes LoRa radio, generates a node identity, responds to CLI
  (`get radio`, `advert`), transmits over the air.
- GPIO4 (TX) / GPIO34 (RX) confirmed as a clean, working UART pair for the
  future LIN transceiver connection — see "How GPIO4/GPIO34 were confirmed"
  above.

🚧 **Written but NOT YET host-tested or hardware-tested:**
- `trumahandshake.{hpp,cpp}` — the LIN master↔slave handshake state machine
  (heartbeat, assign-NAD, multi-frame upload/download). **Known gaps,
  explicitly flagged in code comments: write command_counter is hardcoded
  to 0 instead of incrementing, and the inner status-buffer checksum is not
  computed.** Both must be fixed and tested before any `truma temp/heating/
  boiler` write command is ever sent to a live heater.
- `VictronScanner.{hpp,cpp}` — ESP32 `BLEDevice`/Bluedroid BLE scanning
  glue (ported from an earlier nRF52/Bluefruit version when the target
  board changed). Arduino-specific, can't be unit-tested on the host;
  first real test happens on actual hardware.

❌ **Not started:**
- `Board::handleCommand()` override wiring `truma ...` / `victron status`
  CLI commands into MeshCore's `examples/simple_sensor` role
- The actual PlatformIO build environment/target for `lilygo_tlora_v2_1`
  combining sensor role + Victron BLE + Truma LIN (currently only the
  stock repeater role has been built/flashed, as a hardware pin-verification
  step, not the real target role)
- Any compiled firmware with the Truma/Victron code actually wired in —
  **the pin wiring is confirmed, but the combined firmware is not built yet**

## Recommended bring-up order (once you're physically ready)

1. **Victron-only build first.** Lowest risk, nothing touches the heater.
   Confirms BLE scanning + decrypt + CayenneLPP-over-mesh works before
   adding any LIN code to the mix.
2. **LIN read-only.** Wire the transceiver, flash a build that only listens
   on Serial2 (GPIO4/34) and logs/reports what it sees — zero transmission
   onto the bus. Confirms wiring (splitter, pinout) without any risk of
   disturbing the CP Plus.
3. **LIN slave registration, still read-only otherwise.** Answer heartbeat/
   assign-NAD so the CP Plus recognizes the node as a valid iNet Box peer
   (CP Plus menu: "PR SET" to re-init, per `inetbox.py`'s docs), but still
   never send a `truma temp`-style write.
4. **First write, physically supervised.** Only after the command_counter
   and checksum gaps above are fixed — test one write while watching the
   physical CP Plus display to confirm the setpoint actually changes.

## Repo layout

```
PINOUT_CHECKLIST.md         # multimeter/hardware pin verification steps + confirmed results
firmware/
  trumalin.hpp/.cpp        # LIN transport-layer primitives
  trumaconv.hpp/.cpp       # temperature/mode/error code conversions
  trumastatus.hpp/.cpp     # main status buffer (cmd 0x33) parse/serialize
  trumahandshake.hpp/.cpp  # LIN master<->slave handshake (WIP, see gaps above)
  aes128.hpp/.cpp          # from-scratch AES-128 block cipher
  victronble.hpp/.cpp      # BLE advertisement container + AES-CTR decrypt
  victrondevices.hpp/.cpp  # SmartShunt/SmartSolar bitfield parsers
  VictronScanner.hpp/.cpp  # ESP32 BLEDevice BLE scan glue (not host-testable)
  test/                    # host-side test suite (g++, no hardware)
  run_tests.sh             # runs every test suite above
```

Reference implementations this project ports from (forked under
`github.com/poncheck/...` with `upstream` remotes configured, so updates
from the original authors can be pulled in):
- `danielfett/inetbox.py` — Truma iNet Box LIN slave protocol
- `olivluca/TruMinus` — reference only, NOT used (LIN master approach, see above)
- `keshavdv/victron-ble` — Victron BLE Instant Readout decrypt/parse
- `meshcore-dev/MeshCore` — the mesh firmware this all plugs into (specifically
  the `variants/lilygo_tlora_v2_1` board target)
