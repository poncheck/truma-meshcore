# LilyGO T3 V1.6.1 — pin verification checklist (before wiring the LIN transceiver)

## Why this check is necessary

MeshCore's own build config for this board (`variants/lilygo_tlora_v2_1/platformio.ini`)
already claims these pins — treat them as **definitely occupied**, confirmed
from the source, not guessed:

| Pin | Used for |
|---|---|
| GPIO5 | LoRa SCLK |
| GPIO19 | LoRa MISO |
| GPIO27 | LoRa MOSI |
| GPIO18 | LoRa NSS (chip select) |
| GPIO14 | LoRa RESET |
| GPIO26 | LoRa DIO0 |
| GPIO33 | LoRa DIO1 |
| GPIO25 | TX LED indicator |
| GPIO21 | OLED SDA (I2C) |
| GPIO22 | OLED SCL (I2C) |
| GPIO35 | Battery voltage read (analog **input only**) |
| GPIO0 | User button |

A second reference inside the same MeshCore repo (`repeater_bridge_rs232`
build variant) uses **GPIO34 (RX) / GPIO25 (TX)** for a second UART — but
GPIO25 is already the TX LED pin above. That combination is unverified by
us; don't copy it blindly.

**Separately:** two different pinout diagrams found online for boards sold
as "T3 V1.6.1" disagree with each other on the OLED wiring (one says
SDA=4/SCL=15, another SDA=21/SCL=22 — the second matches what MeshCore's
own config uses). This means **boards with the same printed name may not be
wired identically** — don't trust a diagram found online over what you can
verify on the physical board in hand.

## What to check with a multimeter (continuity mode, board powered OFF)

For each candidate pin below, touch one probe to the GPIO pin's header/pad
and the other probe to the component pin it's suspected of connecting to.
Continuity (beep / ~0 ohms) = connected = NOT free to use.

### 1. Confirm the "definitely occupied" pins above actually match your board
Pick 2-3 from the table (e.g. GPIO21/22 to the OLED's SDA/SCL pins, GPIO18
to the LoRa module's NSS/CS pin) and verify continuity. If these DON'T
match, the whole board may be wired differently than MeshCore assumes —
stop and tell me before going further, something more fundamental is off.

### 2. Rule out ESP32 pins that are risky/unusable regardless of continuity
Even if a pin shows no continuity to anything (looks "free"), these ESP32
pins have special roles and should be avoided for a new UART connection:

| Pin | Why to avoid |
|---|---|
| GPIO12 | Boot "strapping" pin — wrong level at boot can change flash voltage mode, bricking boot |
| GPIO15 | Boot strapping pin |
| GPIO2 | Boot strapping pin, often also tied to the onboard blue LED |
| GPIO0 | Already used (user button) AND a boot strapping pin |
| GPIO1 / GPIO3 | Default USB/UART0 (serial console, programming) — do not repurpose |
| GPIO34 / GPIO36 / GPIO37 / GPIO38 / GPIO39 | **Input-only** on the ESP32 — fine for the transceiver's RX line (data going INTO the XIAO... er, into the ESP32), but physically CANNOT be used as a TX/output pin |

### 3. Candidate pins to test for the LIN transceiver's TX/RX (data lines)

Test continuity on these — if none of the "definitely occupied" items above
light up and they're not on the strapping-pin list, they're good candidates:

- **GPIO4** — commonly free on this board family (watch out: on some other
  "T3" pinout diagrams found online, this was wired to OLED SDA instead of
  GPIO21 — explicitly verify it's NOT connected to the display on your unit)
- **GPIO13** — commonly free
- **GPIO23** — commonly free
- **GPIO32** — commonly free (also usable as input-only-safe choice if preferred)

**Recommendation once verified:** use one input-only-safe pin (e.g. GPIO34,
input-only, confirmed free of the OLED/LoRa connections above) for the
transceiver's **TX -> ESP32 RX** line, and a normal GPIO (e.g. GPIO4 or
GPIO13, once confirmed free) for **ESP32 TX -> transceiver RX**. This
mirrors the existing `repeater_bridge_rs232` pattern (input-only pin for
RX) while avoiding its GPIO25/LED conflict for TX.

### 4. Transceiver module itself

Separately from the ESP32 pins, verify your specific LIN transceiver
module's own pinout against its datasheet/listing photo — "TX"/"RX"
silkscreen labels are sometimes from the module's own microcontroller-side
perspective and sometimes from the bus side; a 50/50 guess here is exactly
the kind of thing worth 30 seconds of multimeter time before it's wired in.

## Report back

Once you've checked, tell me:
1. Did GPIO21/22/18 (or whichever 2-3 you tested) match the "definitely
   occupied" table? (confirms the board matches MeshCore's assumptions)
2. Which of GPIO4 / GPIO13 / GPIO23 / GPIO32 / GPIO34 show NO continuity to
   anything already in use?

Then I'll lock in the exact pin assignment in the firmware config.
