# The flashing page

`docs/` is the site published at <https://devvazquez.github.io/bonsai/>. It
writes a firmware release to a board over USB from the browser, using the Web
Serial API and [esptool-js](https://github.com/espressif/esptool-js) (vendored
under `vendor/esptool-js/`, version in `vendor/esptool-js/VERSION`).

Nothing runs on a server: the release assets are fetched from GitHub by the
browser and pushed straight down the serial port.

## Design

The page borrows its tokens and components from the firmware's own setup portal
(`src/firmware/bonsai-firmware/bonsai-setup.html`): the same off-white ground,
the same step rail, buttons, summary rows and status tints, and the same logo
asset. Flashing a board and setting it up afterwards should not look like two
different products, so when that page's palette changes, `styles.css` follows
it.

## Where the images go

`.github/workflows/release.yml` attaches a `manifest.json` to every release it
publishes and the page reads it, so the offsets come from the build rather than
from guessing. That workflow only runs when someone starts it from the Actions
tab with a version number, so tags and pushes never publish a release on their
own.
Releases assembled some other way still work; the page falls back to, in order:

1. **`manifest.json`** in the [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
   shape. Paths resolve against the release's download URL, so plain file names
   are enough.
2. **A merged image**, one asset whose name contains `merged`, `combined`,
   `factory` or `full`, written at `0x0`.
3. **The PlatformIO set**: `bootloader.bin` (`0x0`), `partitions.bin`
   (`0x8000`), `boot_app0.bin` (`0xe000`), `firmware.bin` (`0x10000`).

Whichever route is taken, the page lists every image and its offset before it
writes anything.

Draft releases are hidden. Pre-releases are listed and marked, and the newest
stable release is the one preselected.

## What flashing does not touch

The board keeps its settings. `/config.json`, the Wi-Fi list and the voice clips
live on the SD card, and nothing here writes to the card. Only the firmware
images are replaced, so a board that was already set up comes back on the
network by itself.

## Enabling the site

The workflow in `.github/workflows/pages.yml` publishes this directory on every
push to `main`, and switches Pages on itself the first time it runs. If the run
fails on that step, turn it on by hand in **Settings, Pages, Build and
deployment, Source: GitHub Actions** and run the workflow again.

## Local preview

Web Serial needs a secure context, which `localhost` counts as:

```sh
python3 -m http.server -d docs 8000   # then open http://localhost:8000
```
