/*
 * Zelda BotW Remote Bomb Prop — ESP32 firmware
 * ESP-WROOM-32 + MAX98357A (I2S amp) + WS2812B LEDs, controlled over BLE
 *
 * Behavior:
 *   Idle          -> LEDs fully off, no sound
 *   1st trigger   -> plays "Bomb_appear01.wav", LEDs soft breathing blue glow
 *   2nd trigger   -> plays "RemoteBomb.wav" (~2s), LEDs flash red for at least
 *                    that long, then LEDs go back off and it's ready to arm again
 *
 * This version plays WAV files directly through the ESP32's built-in I2S
 * driver instead of using the ESP32-audioI2S library. That library is built
 * for streaming/decoding (mp3, internet radio, etc.) and its buffer sizes
 * assume a board with PSRAM; on a plain non-PSRAM ESP-WROOM-32 it can fail
 * to allocate its audio buffer. Since we only need to play two short local
 * WAV clips, a small hand-rolled player avoids that dependency completely
 * and uses only a couple KB of RAM.
 *
 * Libraries required (install via Arduino Library Manager):
 *   - NimBLE-Arduino          by h2zero
 *   - Adafruit NeoPixel       by Adafruit
 *   (LittleFS and the I2S driver both come bundled with the ESP32 board package)
 *
 * Audio files:
 *   Put your own "Bomb_appear01.wav" and "RemoteBomb.wav" (16-bit PCM WAV,
 *   mono or stereo, any standard sample rate) in this sketch's /data folder,
 *   then upload with the LittleFS/SPIFFS uploader tool.
 *
 * Board settings: any standard ESP32 dev module board definition works.
 */

#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>
#include <LittleFS.h>
#include <driver/i2s.h>

// Defined up here (before any function that uses it) so Arduino's
// auto-generated function prototypes - which it inserts near the top of the
// file - can see this type instead of erroring on an unknown type.
struct WavInfo {
  uint32_t sampleRate = 44100;
  uint16_t bitsPerSample = 16;
  uint16_t numChannels = 1;
  uint32_t dataSize = 0;
};

// ---------- Pin map ----------
#define LED_PIN      27
#define NUM_LEDS     36

#define I2S_BCLK     26
#define I2S_LRC      25
#define I2S_DOUT     22
#define I2S_PORT     I2S_NUM_0

// ---------- BLE UUIDs (must match the web app) ----------
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Minimum time (ms) the red flash stays active, so it always covers the
// ~2 second explosion clip even if the file is a little shorter/longer.
#define BOOM_MIN_DURATION_MS 2200

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pChar = nullptr;
bool deviceConnected = false;

enum BombState { IDLE, ARMED, BOOM };
BombState state = IDLE;
unsigned long stateStart = 0;

volatile float g_volume = 0.8f; // 0.0-1.0, controlled by the app's slider

// Forward declarations (needed because these are called from classes defined
// below, before Arduino's auto-prototype generator has a chance to see them)
void notifyState(const char* s);
void handleTrigger();

// ---------- BLE callbacks ----------
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& connInfo) override {
    deviceConnected = true;
    Serial.println("Phone connected");
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& connInfo, int reason) override {
    deviceConnected = false;
    Serial.println("Phone disconnected, re-advertising");
    NimBLEDevice::startAdvertising();
  }
};

class TriggerCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    std::string v = std::string(c->getValue().c_str());
    if (v.empty()) return;

    if (v[0] == 'T') {
      handleTrigger();
    } else if (v[0] == 'V' && v.size() >= 3 && v[1] == ':') {
      // Volume command, format "V:<0-100>" (percent)
      int pct = atoi(v.substr(2).c_str());
      pct = constrain(pct, 0, 100);
      g_volume = pct / 100.0f;
      Serial.printf("Volume set to %d%%\n", pct);
    }
  }
};

void notifyState(const char* s) {
  if (pChar && deviceConnected) {
    pChar->setValue(s);
    pChar->notify();
  }
}

// ---------- LED effects ----------
void setAll(uint32_t c) {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, c);
  strip.show();
}

void idleEffect() {
  setAll(strip.Color(0, 0, 0)); // fully off
}

void armedEffect() {
  // gentle, constant breathing blue glow
  const float periodMs = 1800.0;
  float phase = fmod((float)millis(), periodMs) / periodMs;
  float b = (sin(phase * 2 * PI) + 1) / 2.0; // 0..1
  int brightness = 20 + (int)(b * 180);      // 20..200
  setAll(strip.Color(0, brightness / 3, brightness)); // soft cyan-blue
}

void boomEffect() {
  unsigned long elapsed = millis() - stateStart;

  if (elapsed < BOOM_MIN_DURATION_MS) {
    // bright red immediately, then eases down to off over the rest of the duration
    float t = (float)elapsed / (float)BOOM_MIN_DURATION_MS; // 0..1
    float decay = 1.0f - (t * t);                           // eased falloff, stays brighter longer up front
    int brightness = (int)(255 * decay);
    if (brightness < 0) brightness = 0;
    setAll(strip.Color(brightness, 0, 0));
  } else {
    setAll(strip.Color(0, 0, 0));
  }
}

// ---------- WAV playback (built-in I2S driver, no external audio library) ----------
bool parseWav(File &f, WavInfo &info) {
  f.seek(0);
  char tag[4];
  uint32_t skip32;

  if (f.read((uint8_t*)tag, 4) != 4 || memcmp(tag, "RIFF", 4) != 0) return false;
  f.read((uint8_t*)&skip32, 4); // RIFF chunk size, unused
  if (f.read((uint8_t*)tag, 4) != 4 || memcmp(tag, "WAVE", 4) != 0) return false;

  while (f.available()) {
    char id[4];
    uint32_t size;
    if (f.read((uint8_t*)id, 4) != 4) break;
    if (f.read((uint8_t*)&size, 4) != 4) break;

    if (memcmp(id, "fmt ", 4) == 0) {
      uint16_t audioFormat, numChannels, blockAlign, bitsPerSample;
      uint32_t sampleRate, byteRate;
      f.read((uint8_t*)&audioFormat, 2);
      f.read((uint8_t*)&numChannels, 2);
      f.read((uint8_t*)&sampleRate, 4);
      f.read((uint8_t*)&byteRate, 4);
      f.read((uint8_t*)&blockAlign, 2);
      f.read((uint8_t*)&bitsPerSample, 2);
      info.numChannels = numChannels;
      info.sampleRate = sampleRate;
      info.bitsPerSample = bitsPerSample;
      if (size > 16) f.seek(size - 16, SeekCur); // skip any extra fmt bytes
    } else if (memcmp(id, "data", 4) == 0) {
      info.dataSize = size;
      return true; // data chunk found and file cursor is positioned right after its header
    } else {
      f.seek(size, SeekCur); // skip chunk we don't care about (e.g. LIST, INFO)
    }
    if (size % 2 == 1) f.seek(1, SeekCur); // chunks are word-aligned
  }
  return false;
}

static uint8_t s_readBuf[1024];
static uint8_t s_writeBuf[2048];

bool playWav(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    Serial.printf("Could not open %s\n", path);
    return false;
  }

  WavInfo info;
  if (!parseWav(f, info) || info.bitsPerSample != 16) {
    Serial.printf("Unsupported/invalid WAV: %s (need 16-bit PCM)\n", path);
    f.close();
    return false;
  }

  i2s_set_clk(I2S_PORT, info.sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);

  uint32_t remaining = info.dataSize;
  while (remaining > 0) {
    size_t toRead = min((uint32_t)sizeof(s_readBuf), remaining);
    size_t got = f.read(s_readBuf, toRead);
    if (got == 0) break;
    remaining -= got;

    int16_t* samples = (int16_t*)s_readBuf;
    size_t numSamples = got / 2;

    // apply software volume with clipping
    for (size_t i = 0; i < numSamples; i++) {
      int32_t v = (int32_t)(samples[i] * g_volume);
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      samples[i] = (int16_t)v;
    }

    size_t bytesWritten = 0;
    if (info.numChannels == 1) {
      // duplicate each mono sample into a stereo L+R frame
      int16_t* out = (int16_t*)s_writeBuf;
      for (size_t i = 0; i < numSamples; i++) {
        out[i * 2]     = samples[i];
        out[i * 2 + 1] = samples[i];
      }
      i2s_write(I2S_PORT, s_writeBuf, numSamples * 4, &bytesWritten, portMAX_DELAY);
    } else {
      i2s_write(I2S_PORT, s_readBuf, got, &bytesWritten, portMAX_DELAY);
    }
  }

  f.close();
  return true;
}

volatile bool triggerBusy = false;

void triggerTask(void* param) {
  if (state == IDLE) {
    state = ARMED;
    stateStart = millis();
    notifyState("ARMED");
    Serial.println("-> ARMED (blue glow)");
    playWav("/Bomb_appear01.wav"); // blocks this task briefly; LEDs keep animating via loop() independently
    // stays ARMED after the clip finishes - blue glow continues until the next press
  } else if (state == ARMED) {
    state = BOOM;
    stateStart = millis();
    notifyState("BOOM");
    Serial.println("-> BOOM (red flash)");
    playWav("/RemoteBomb.wav");
    // make sure the flash lasts at least BOOM_MIN_DURATION_MS even if the clip is shorter
    while (millis() - stateStart < BOOM_MIN_DURATION_MS) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    state = IDLE;
    notifyState("IDLE");
    Serial.println("-> IDLE (off, ready to arm)");
  }
  triggerBusy = false;
  vTaskDelete(NULL);
}

void handleTrigger() {
  // Runs on the BLE host task - keep this fast and non-blocking. All the
  // actual playback/timing work happens in triggerTask on its own task, so
  // notify() calls go out immediately instead of getting stuck queued
  // behind several seconds of blocking audio playback.
  if (triggerBusy) return; // ignore taps while a sequence is already running
  triggerBusy = true;
  xTaskCreate(triggerTask, "triggerTask", 4096, nullptr, 1, nullptr);
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);

  strip.begin();
  strip.show(); // starts fully off

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed — did you upload the /data folder?");
  }

  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_driver_install(I2S_PORT, &i2sConfig, 0, NULL);

  i2s_pin_config_t pinConfig = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_set_pin(I2S_PORT, &pinConfig);

  NimBLEDevice::init("BotW Bomb");
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(SERVICE_UUID);
  pChar = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  pChar->setCallbacks(new TriggerCallback());
  pService->start();

  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(SERVICE_UUID);
  pAdv->setName("BotW Bomb");     // NimBLE 2.x doesn't advertise the name by default anymore
  pAdv->enableScanResponse(true); // puts the name in the scan response so it fits alongside the service UUID
  pAdv->start();

  Serial.println("Advertising as 'BotW Bomb' — open the web app and tap Connect.");
}

void loop() {
  // handleTrigger() (called from the BLE task when a button press comes in)
  // blocks briefly while it plays audio - this loop keeps running independently
  // on its own task the whole time, so the LEDs stay animated based on 'state'.
  switch (state) {
    case IDLE:  idleEffect();  break;
    case ARMED: armedEffect(); break;
    case BOOM:  boomEffect();  break;
  }
}
