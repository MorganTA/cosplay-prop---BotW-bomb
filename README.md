# Zelda: Breath of the Wild — Remote Bomb Prop

A functional remote bomb prop inspired by Zelda: Breath of the Wild. Control it wirelessly from an Android phone over Bluetooth Low Energy. Press the button once to arm it (blue glow + appear sound), press again to detonate (red flash + explosion sound).

---

## Demo

> Add a photo or video of your finished prop here.

---

## Features

- Bluetooth Low Energy control via a web app (no app install required on Android)
- Blue breathing LED glow when armed
- Red fade-to-off LED explosion effect on detonation
- I2S audio playback through a MAX98357A amplifier
- Volume slider in the control app
- Works fully offline after first load (PWA with service worker caching)
- Can be installed as a standalone Android app (no address bar) via PWABuilder

---

## Hardware

| Part | Notes |
|---|---|
| ESP-WROOM-32 dev board | Any standard ESP32 Dev Module works |
| MAX98357A I2S amp | Digital amp + DAC in one chip, no separate DAC needed |
| Speaker | 4Ω or 8Ω, 3W |
| WS2812B addressable LEDs | Strip or ring, 36 LEDs |
| LiPo battery | 3.7V |
| TP4056 charge module | For charging the LiPo via USB |
| MT3608 boost converter | Steps LiPo voltage up to 5V |
| Latching push button switch | Inline on the 5V power rail for on/off |
| 330Ω resistor | In series on the WS2812B data line |
| 470–1000µF capacitor | Across the 5V/GND rail near the LED strip |

---

## Wiring

### MAX98357A → ESP32

| MAX98357A | ESP32 |
|---|---|
| VIN | 5V |
| GND | GND |
| BCLK | GPIO26 |
| LRC (WS) | GPIO25 |
| DIN | GPIO22 |
| GAIN | GND (12dB), or 100kΩ resistor to GND (15dB max) |
| SD | VIN (always enabled) |

Connect the speaker's two leads to the MAX98357A's `+` and `−` output terminals.

### WS2812B LEDs → ESP32

| LED | ESP32 |
|---|---|
| VCC | 5V |
| GND | GND |
| DIN | GPIO27 (through a 330Ω resistor) |

### Power rail

```
LiPo (+) → TP4056 IN+ → TP4056 OUT+ → Boost converter IN+
→ Boost converter OUT+ (5V) → [SWITCH] → ESP32 5V / MAX98357A VIN / LED VCC
```

All GND connections share a common rail. Place a 470–1000µF capacitor across 5V/GND near the LED strip to prevent brownouts during bright flashes.

> **Tip:** Wire the switch after the boost converter, not before the TP4056. This way the battery charges normally via USB even when the prop is switched off.

---

## Firmware

### Prerequisites

- [Arduino IDE 2.x](https://www.arduino.cc/en/software)
- ESP32 board package by Espressif Systems **3.0 or later** (add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to Additional Board Manager URLs)
- [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload) plugin for Arduino IDE 2.x

### Libraries (install via Library Manager)

- **NimBLE-Arduino** by h2zero
- **Adafruit NeoPixel** by Adafruit

> The I2S driver and LittleFS are both bundled with the ESP32 board package — no extra install needed.

### Board settings

| Setting | Value |
|---|---|
| Board | ESP32 Dev Module |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| PSRAM | Disabled |

### Audio files

This project does not include audio — you must supply your own WAV files (royalty-free sources like [freesound.org](https://freesound.org) work well, or record your own).

Prepare two files:
- `Bomb_appear01.wav` — played when the bomb is armed
- `RemoteBomb.wav` — played on detonation (~2 seconds)

Both must be **16-bit PCM WAV** format. Mono or stereo both work. 22050 Hz or 44100 Hz sample rate recommended. You can convert and export in this format using [Audacity](https://www.audacityteam.org/) (File > Export > Export as WAV > Encoding: 16-bit PCM).

Place both files in a folder named `data` inside the sketch folder:

```
botw_bomb_firmware/
├── botw_bomb_firmware.ino
└── data/
    ├── Bomb_appear01.wav
    └── RemoteBomb.wav
```

### Flashing

**Step 1 — Upload the audio files (do this first)**

1. Close the Serial Monitor if it's open (the port must be free)
2. Select the correct board and COM port under Tools
3. Open the Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`) and run **Upload LittleFS to Pico/ESP32**
4. Wait for it to finish before continuing

**Step 2 — Upload the sketch**

1. Click the **Upload** button as normal
2. Open Serial Monitor at **115200 baud**
3. Confirm you see: `Advertising as 'BotW Bomb' — open the web app and tap Connect.`

> If you change the partition scheme at any point, re-upload the LittleFS filesystem too — changing the partition layout moves where the filesystem lives in flash.

---

## Control App

The web app (`bomb_detonator.html`) is a Progressive Web App that runs in Chrome on Android. It communicates with the ESP32 over Web Bluetooth.

### Hosting

Web Bluetooth requires a secure origin (HTTPS or localhost). Host the app files on any free static host:

- [GitHub Pages](https://pages.github.com) (free, recommended)
- [Netlify Drop](https://app.netlify.com/drop) (no account needed, drag and drop)

Upload all five files together:

```
bomb_detonator.html
manifest.json
sw.js
icon-192.png
icon-512.png
```

### Offline use

The service worker (`sw.js`) caches everything after the first successful load over a network connection. Once cached, the app works fully offline. Open it once while connected to WiFi to prime the cache.

> Whenever you update the app files, bump the `CACHE_NAME` version string in `sw.js` (e.g. `v4` → `v5`) so the service worker fetches and re-caches the updated files next time it's online.

### Installing as a standalone Android app (no address bar)

Use [PWABuilder](https://www.pwabuilder.com) to generate a signed APK:

1. Enter your hosted HTTPS URL and click **Start**
2. Choose **Android** as the package type
3. Set your app name and choose **Create new** for the signing key — save the keystore file and credentials somewhere safe
4. Download the generated ZIP and copy the `.apk` file to your phone via USB
5. Upload the included `assetlinks.json` to your hosted site at `/.well-known/assetlinks.json` — this proves domain ownership so the app can go truly fullscreen
6. Install the APK on your phone (Settings > Apps > allow installs from your file manager if prompted)
7. Open the app once while online to let the service worker cache everything

### Usage

1. Open the app and tap **Connect Bomb** — select **BotW Bomb** from the device picker
2. Tap the bomb icon once to arm (blue glow + appear sound)
3. Tap again to detonate (red flash + explosion sound)
4. Use the volume slider to adjust playback volume

---

## Customisation

| Thing to change | Where |
|---|---|
| Number of LEDs | `#define NUM_LEDS` in the firmware |
| LED data pin | `#define LED_PIN` in the firmware |
| I2S pins | `#define I2S_BCLK / I2S_LRC / I2S_DOUT` in the firmware |
| Explosion duration | `#define BOOM_MIN_DURATION_MS` in the firmware |
| Audio filenames | `playWav()` calls in `handleTrigger()` |
| BLE device name | `NimBLEDevice::init()` and `scanResponseData.setName()` in setup |

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `Could not open COM port` | Close the Serial Monitor before uploading |
| `Sketch too big` | Set Partition Scheme to **Huge APP (3MB No OTA/1MB SPIFFS)** |
| `OOM: failed to allocate AudioBuffer` | Make sure PSRAM is set to **Disabled** in board settings |
| `override` compile errors | Update the ESP32 board package to 3.x |
| No audio, no error | Check SD pin is wired to VIN on the MAX98357A; verify BCLK/LRC/DIN wiring |
| BLE shows as Unknown Device | Re-flash after confirming `scanResponseData.setName()` is in setup |
| App icon not going red | Hard-refresh Chrome after updating the hosted files |
| APK won't install | Try transferring via USB cable; check for a Play Protect warning to dismiss |
| App needs internet to launch | Open it once while on WiFi to let the service worker cache everything |

---

## License

This project is for personal/educational use. Audio files are not included — source your own royalty-free sounds. Zelda and Breath of the Wild are trademarks of Nintendo.
