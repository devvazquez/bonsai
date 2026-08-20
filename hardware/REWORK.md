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
touched the bus yet; `Audio::begin()` ran next and drove GPIO8 low to hold the
amplifier off. From there `/config.json` cannot be read or written, `/audio`
cannot be created, and the credentials entered over the AP are lost on every
boot, so each one looks like the first.

## The fix

**Firmware — ownership of GPIO8, not its level.** `SPI.begin()` routes that pad
to the SPI peripheral's MISO input. Any `pinMode()` on it points it back at
plain GPIO and the peripheral is left reading a disconnected input — and that is
true of `INPUT` and `INPUT_PULLUP` exactly as much as of `OUTPUT`. The card
still mounts, because identification is short and forgiving, and then every
transfer comes back empty. So nothing outside `playWav()` touches the pin, and
`playWav()` only takes it once the clip is in PSRAM and the file is closed,
then hands it straight back with `spiAttachMISO()`.

Between clips the pin stays with the SPI peripheral and `SD_MODE` is held at
ground by its own internal 100k pull-down, which shuts the amplifier down for
free. That works because this is the bare MAX98357A and not a breakout, whose
100k to VDD would instead have left it on.

**No second rework is needed, and no pull-up.** Two things that look like the
cause here are not:

- *The 100k pull-down on `SD_MODE` dragging MISO down.* It does sit on the net,
  and the arithmetic is unflattering — against the chip's ~45k internal pull-up
  it divides to 2.28 V, under the 2.475 V an ESP32-S3 wants for a HIGH. It still
  is not the fault: with the pin left alone the bus runs fine at full speed.
  Adding a 10k to 3V3 on D9 fixes nothing that is broken.
- *The bus clock being too fast.* A card that mounts and then fails every
  transfer is the classic picture of an over-fast clock, because mounting
  happens at the 400 kHz the spec mandates for identification. Dropping to
  400 kHz changes nothing here; the board reads and writes happily at 4 MHz.

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
stream a clip straight off the card again instead of buffering it into PSRAM
first, and would drop the `spiAttachMISO()` dance in `Audio::ampRelease()`
along with the standing risk that some future code path calls `pinMode()` on
GPIO8 and quietly breaks the card all over again.
