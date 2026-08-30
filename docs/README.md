# Bonsai web flasher

The page in this directory is the site published at
<https://devvazquez.github.io/bonsai/>. It writes a firmware release to a board
over USB from the browser, using the Web Serial API and
[esptool-js](https://github.com/espressif/esptool-js) (vendored under
`vendor/esptool-js/`, version in `vendor/esptool-js/VERSION`).

Nothing runs server side: the release assets are fetched from GitHub by the
browser and pushed straight down the serial port.

## Enabling the site

The workflow in `.github/workflows/pages.yml` publishes this directory on every
push to `main`. It needs Pages switched on once, in
**Settings → Pages → Build and deployment → Source: GitHub Actions**.

## What a release has to contain

The page reads the repository's releases through the GitHub API and works out
where each asset belongs. It looks, in this order, for:

1. **`manifest.json`** — an [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
   style manifest attached to the release. Paths are resolved against the
   release's download URL, so plain file names are enough:

   ```json
   {
     "name": "Bonsai",
     "version": "1.0.0",
     "builds": [
       {
         "chipFamily": "ESP32-S3",
         "parts": [
           { "path": "bootloader.bin", "offset": 0 },
           { "path": "partitions.bin", "offset": 32768 },
           { "path": "boot_app0.bin",  "offset": 57344 },
           { "path": "firmware.bin",   "offset": 65536 }
         ]
       }
     ]
   }
   ```

2. **A merged image** — one asset whose name contains `merged`, `combined`,
   `factory` or `full`, written at `0x0`. This is what
   `esptool.py merge_bin` produces.

3. **The usual PlatformIO set** — `bootloader.bin` (`0x0`),
   `partitions.bin` (`0x8000`), `boot_app0.bin` (`0xe000`) and
   `firmware.bin` (`0x10000`).

Whichever route is taken, the resolved offsets are shown on the page before
anything is written, so a wrong layout is visible rather than surprising.

Draft releases are skipped; pre-releases are listed and marked, and the newest
stable release is preselected.

## Local preview

Web Serial needs a secure context, which `localhost` counts as:

```sh
python3 -m http.server -d docs 8000   # then open http://localhost:8000
```
