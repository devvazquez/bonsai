# Board rework: the microSD bus

The XIAO ESP32-S3 **Sense** puts its microSD slot on the same pads the header
brings out, and the first Bonsai board hands two of them to something else:

| Net | U1 pad | GPIO | XIAO alias | Also used by the microSD as |
| --- | ------ | ---- | ---------- | --------------------------- |
| `BT1` (SW1 pad 1) | 11 | GPIO9 | D10 | **MOSI** |
| `SD` (U2 pad 4, MAX98357A shutdown) | 10 | GPIO8 | D9 | **MISO** |

The slot's other two lines are SCK on GPIO7 (D8) and CS on GPIO21, which nothing
else touches.

## What it looked like

Flash the board, configure the WiFi over the AP, reboot — and every operation on
the card fails. `SD.begin()` itself succeeds, because at that point nothing has
touched the bus yet; `Audio::begin()` runs next and used to drive GPIO8 low to
hold the amplifier off, which clamps the card's data-out line to ground. From
there `/config.json` cannot be read or written, `/audio` cannot be created, and
the credentials entered over the AP are lost on every boot, so each one looks
like the first.

## The fix

**Firmware — the amplifier's SD_MODE pin.** The net carries no resistor: U1
pad 10 runs straight to U2 pad 4, so `SD_MODE` sits directly on MISO. It is only
driven while nothing is reading the card, and left as an input the rest of the
time. That works because this is the bare MAX98357A and not a breakout:
`SD_MODE` has an internal 100k pull-down, so releasing the pin takes it to
ground and shuts the amplifier down with no external part — where a breakout's
100k to VDD would have left it on. Driven to 3V3 it is above the 1.4 V that
selects the left channel. `playWav()` buffers the whole clip into PSRAM and
closes the file before taking the line, so no second cut is needed.

The 100k pull-down is also the only thing this net adds to MISO — about 33 uA,
which the card's driver does not notice.

**PCB — the button.** Cut the `BT1` trace between SW1 pad 1 and U1 pad 11
(GPIO9/MOSI) and run a wire from SW1 pad 1 to **U1 pad 4 — GPIO4, D3**, the
nearest pad nothing else claims. SW1 pad 2 already goes to GND, so the switch
stays active-low against the internal pull-up and `BUTTON_PIN` is 4.

Do the cut on the U1 side of SW1 pad 1 so the pad keeps its own copper, and
check continuity between SW1 pad 1 and U1 pad 11 afterwards — it should read
open.

## Pads still free on the header

D4 (GPIO5/SDA), D5 (GPIO6/SCL), D6 (GPIO43/TX), D7 (GPIO44/RX).

## For the next revision

Route `BT1` to GPIO4 in the schematic so the board needs no rework. Giving the
amplifier's `SD` net its own free pad — D4 (GPIO5) — would also let the firmware
cut the amplifier between clips and stream straight off the card again, instead
of buffering into PSRAM first.
