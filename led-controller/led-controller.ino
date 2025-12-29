#include <Arduino.h>

#include <FastLED.h>
#include "StairChaser.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <string>

#include "HT_st7735.h"
HT_st7735 st7735;

// ================= LED / CHASER =================
#define DATA_PIN    5
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS    150
CRGB leds[NUM_LEDS];

const float MAX_Y_CM = 300.0f;

// Pick your straight segment on the strip (inclusive)
const uint16_t STRIP_START_LED = 22;
const uint16_t STRIP_END_LED   = 118;

// Chaser instance (range-aware constructor)
StairChaser chaser(leds, NUM_LEDS, MAX_Y_CM, STRIP_START_LED, STRIP_END_LED);

// If we don’t receive a BLE update for this long -> no target
const uint32_t TARGET_TIMEOUT_MS = 300;

// Latest target data (written by BLE callback, used in loop)
volatile int16_t g_x_mm = 0;
volatile int16_t g_y_mm = 0;
volatile bool    g_hasTarget = false;
volatile uint32_t g_lastTargetMs = 0;

// ================= UUIDs =================
#define CONTROL_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_COLOR_UUID "4fafc201-1fb5-459e-8fcc-c5c9c3319101"

#define CHAR_BRIGHTNESS_UUID "41db7a25-59f1-405e-b349-13afcfb5c806"

#define STATUS_SERVICE_UUID "71e1ebf9-7833-49a0-9fb5-580a753153de"
#define CHAR_RANGE_UUID "d6d17c21-5d8c-4942-83c2-6fdbd46a2989"

#define POSITION_SERVICE_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a7"
#define CHAR_POSITION_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = nullptr;
BLECharacteristic* pColorChar = nullptr;
BLECharacteristic* pBrightnessChar = nullptr;
BLECharacteristic* pPositionChar = nullptr;
BLECharacteristic* pRangeChar = nullptr;

volatile int connectedDevices = 0;

// ================= Callbacks =================
class ColorCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String hex = pCharacteristic->getValue().c_str();
    hex.trim();

    Serial.print("Raw color from BLE: ");
    Serial.println(hex);

    if (hex.length() == 0) return;
    if (hex[0] == '#') hex.remove(0, 1);
    if (hex.length() != 6) {
      Serial.println("Invalid color length, expected 6 hex chars");
      return;
    }
    // read the string values as hex numbers
    uint8_t r = strtol(hex.substring(0, 2).c_str(), nullptr, 16);
    uint8_t g = strtol(hex.substring(2, 4).c_str(), nullptr, 16);
    uint8_t b = strtol(hex.substring(4, 6).c_str(), nullptr, 16);
    
    Serial.print("Parsed RGB = ");
    Serial.print(r);
    Serial.print(",");
    Serial.print(g);
    Serial.print(",");
    Serial.println(b);

    uint16_t color565 =
      ((r & 0xF8) << 8) |
      ((g & 0xFC) << 3) |
      (b >> 3);

    st7735.st7735_fill_screen(color565);
    st7735.st7735_write_str(
      0, 0, hex.c_str(),
      Font_11x18,
      ST7735_WHITE,
      color565
    );
  }
};

// ===== Control->Brightness characteristic callbacks =====
class BrightnessCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String v = pCharacteristic->getValue();

    if (v.length() != 1) {
      Serial.print("Brightness expected 1 byte, got ");
      Serial.println(v.length());
      return;
    }

    const uint8_t* data = (const uint8_t*)v.begin();
    uint8_t br = data[0];

    Serial.print("Brightness = ");
    Serial.println(br);

    FastLED.setBrightness(br);
  }
};

volatile bool g_newSample = false;
class RadarCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String v = pCharacteristic->getValue();
    if (v.length() < 4) return;

    const uint8_t* data = (const uint8_t*)v.begin();
    int16_t x_mm = (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    int16_t y_mm = (int16_t)((uint16_t)data[2] | ((uint16_t)data[3] << 8));

    // ignore readings with too large |x|
    if (abs(x_mm) > 400) {
      // don’t refresh lastTargetMs → keeps timer counting for smooth fade
      g_hasTarget = false;
      return;
    }
    Serial.printf("x=%d y=%d\n", (int)x_mm, (int)y_mm);
    // normal valid target
    g_hasTarget = !(x_mm == 0 && y_mm == 0);
    if (g_hasTarget) {
      g_x_mm = x_mm;
      g_y_mm = y_mm;
      g_lastTargetMs = millis();
    }

    g_newSample = true;
  }
};


class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    connectedDevices++;
    Serial.print("Device connected. Total: ");
    Serial.println(connectedDevices);
    BLEDevice::startAdvertising(); // keep advertising for multi-connect
  }

  void onDisconnect(BLEServer* pServer) override {
    connectedDevices--;
    if (connectedDevices < 0) connectedDevices = 0;
    Serial.print("Device disconnected. Total: ");
    Serial.println(connectedDevices);
  }
};

// ================= Range notify helper =================
void sendRange(uint8_t start, uint8_t end) {
  if (!pRangeChar) return;
  uint8_t payload[2] = { start, end };
  pRangeChar->setValue(payload, sizeof(payload));
  pRangeChar->notify();
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting BLE StairLight + StairChaser...");

  g_lastTargetMs = millis();

  // Display init
  st7735.st7735_init();
  st7735.st7735_fill_screen(ST7735_BLACK);
  st7735.st7735_write_str(0, 0, "StairLight", Font_11x18, ST7735_WHITE, ST7735_BLACK);

  // LED init
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);

  // ===== NEW CHASER CONFIG =====
  chaser.setPhysicalEnds(0, NUM_LEDS - 1);   // full strip path
  chaser.setTailLen(20);
  chaser.setColors(CRGB::White, CRGB::Black);

  chaser.setLeadOffsets(20, 10);            // +20 down, -10 up
  chaser.setSpeedLedsPerSecond(90);         // fast but no skipping
  chaser.setMaxStepsPerUpdate(4);           // prevents jumps if loop stalls

  // Make this a bit bigger than your BLE timeout so brief loss doesn't immediately finish:
  chaser.setLostTimeoutMs(350);


  chaser.setFadeOutMs(450);                 // disperse time
  chaser.setDirectionDeadbandCm(3.0f);      // ignore small jitter

  // If your radar Y is inverted, uncomment:
  // chaser.setInvertY(true);

  chaser.begin(80);

  // BLE init
  BLEDevice::init("StairLight");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // Service 1: Control
  BLEService* pControlService = pServer->createService(CONTROL_SERVICE_UUID);

  pColorChar = pControlService->createCharacteristic(
    CHAR_COLOR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pColorChar->setCallbacks(new ColorCallbacks());
  pColorChar->setValue("000000");

  pBrightnessChar = pControlService->createCharacteristic(
    CHAR_BRIGHTNESS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pBrightnessChar->setCallbacks(new BrightnessCallbacks());
  uint8_t br = 80;
  pBrightnessChar->setValue(&br, 1);

  pControlService->start();

  // Service 2: Positioning
  BLEService* pPositionService = pServer->createService(POSITION_SERVICE_UUID);

  pPositionChar = pPositionService->createCharacteristic(
    CHAR_POSITION_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pPositionChar->setCallbacks(new RadarCallbacks());

  pPositionService->start();

  // Service 3: Status
  BLEService* pStatusService = pServer->createService(STATUS_SERVICE_UUID);

  pRangeChar = pStatusService->createCharacteristic(
    CHAR_RANGE_UUID,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );
  pRangeChar->addDescriptor(new BLE2902());

  pStatusService->start();

  // Advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(POSITION_SERVICE_UUID);
  pAdvertising->addServiceUUID(CONTROL_SERVICE_UUID);
  pAdvertising->addServiceUUID(STATUS_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Advertising 'StairLight'...");
}

// ================= Loop =================
void loop() {
  // ---- clean debug print (max 10x/sec) ----
  static uint32_t lastPrint = 0;
  if (g_newSample && millis() - lastPrint > 100) {
    lastPrint = millis();
    g_newSample = false;

    int16_t x_mm = g_x_mm;
    int16_t y_mm_dbg = g_y_mm;
    bool ht_dbg = g_hasTarget;

    // Serial.printf("Radar XY: x=%d mm, y=%d mm, hasTarget=%d\n",
    //               x_mm, y_mm_dbg, ht_dbg ? 1 : 0);
  }

  // ---- timeout -> treat as no target ----
  bool hasTarget = g_hasTarget;
  int16_t y_mm = g_y_mm;
  uint32_t lastMs = g_lastTargetMs;

  if (millis() - lastMs > TARGET_TIMEOUT_MS) {
    hasTarget = false;
  }

  // ---- convert y -> cm (ignore x for chaser) ----
  float y_cm = 0.0f;
  if (hasTarget) {
    y_cm = fabsf((float)y_mm) / 10.0f;  // mm -> cm
  }

  // ---- update chaser ----
  chaser.update(hasTarget, y_cm);

  // ---- notify range once per second ----
  static uint32_t lastRange = 0;
  if (millis() - lastRange > 200) {   // 5x/sec (change to 1000 if you want 1x/sec)
    lastRange = millis();

    uint16_t a, b;
    if (chaser.getActiveRange(a, b)) {
      Serial.print("Start= ");
      Serial.println(a);
      Serial.print("end= ");
      Serial.println(b);

      sendRange((uint8_t)a, (uint8_t)b);
    } else {
      // nothing active / fully faded
      sendRange(0, 0);
    }
  }


  delay(5);
}
