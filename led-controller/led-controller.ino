#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "HT_st7735.h"
#include "Arduino.h"

HT_st7735 st7735;

// ===== Service & characteristic UUIDs =====
#define SERVICE_UUID          "4fafc201-1fb5-459e-8fcc-c5c9c331914b"   // bestaande StairLight-service
#define CHAR_NOTIFY_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"   // counter notify
#define CHAR_COLOR_UUID       "4fafc201-1fb5-459e-8fcc-c5c9c3319101"   // hex color R/W

// NEW: aparte service voor positie (radar node)
#define POSITION_SERVICE_UUID "beb5483e-36e1-4688-b7f5-ea07361b2600"
#define CHAR_RADAR_UUID       "beb5483e-36e1-4688-b7f5-ea07361b2601"


BLEServer*         pServer       = nullptr;
BLECharacteristic* pNotifyChar   = nullptr;
BLECharacteristic* colorChar     = nullptr;
BLECharacteristic* radarChar     = nullptr;   

volatile int  connectedDevices = 0;
uint32_t      counterValue     = 0;

// ===== Color characteristic callbacks =====
class ColorCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    // FIX: use Arduino String, not std::string
    String hex = pCharacteristic->getValue();  
    hex.trim();

    Serial.print("Raw color from BLE: ");
    Serial.printf("%u", pCharacteristic->getHandle());
    Serial.println(hex);

    if (hex.length() == 0) return;

    // Optional leading '#'
    if (hex[0] == '#') {
      hex.remove(0, 1);
    }

    if (hex.length() != 6) {
      Serial.println("Invalid color length, expected 6 hex chars");
      return;
    }

    int r = strtol(hex.substring(0, 2).c_str(), nullptr, 16);
    int g = strtol(hex.substring(2, 4).c_str(), nullptr, 16);
    int b = strtol(hex.substring(4, 6).c_str(), nullptr, 16);

    Serial.print("Parsed RGB = ");
    Serial.print(r); Serial.print(",");
    Serial.print(g); Serial.print(",");
    Serial.println(b);

    // Convert to 16-bit 565 color
    uint16_t color565 =
        ((r & 0xF8) << 8) |   // top 5 bits of red
        ((g & 0xFC) << 3) |   // top 6 bits of green
        (b >> 3);             // top 5 bits of blue

    // Fill screen with color + show text
    st7735.st7735_fill_screen(color565);
    st7735.st7735_write_str(
      0, 0,
      hex.c_str(),
      Font_11x18,
      ST7735_WHITE,   // text color
      color565        // background color
    );
  }
};


class RadarCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    // Heltec BLE library: getValue() returns Arduino String
    String value = pCharacteristic->getValue();  // binary data

    int len = value.length();
    if (len < 4) {
      Serial.print("Radar write too short: ");
      Serial.println(len);
      return;
    }

    // Treat underlying buffer as raw bytes
    const uint8_t* data = (const uint8_t*)value.c_str();

    int16_t x_mm = (int16_t)(data[0] | (data[1] << 8));  // little-endian
    int16_t y_mm = (int16_t)(data[2] | (data[3] << 8));  // little-endian

    Serial.print("Radar XY from node (positionService): x=");
    Serial.print(x_mm);
    Serial.print(" mm, y=");
    Serial.print(y_mm);
    Serial.println(" mm");

    // TODO: here later:
    //  - mapToLedIndex(x_mm, y_mm)
    //  - LED animations
  }
};


// ===== Server connect/disconnect callbacks (multi-connect) =====
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    connectedDevices++;
    Serial.print("Device connected. Total: ");
    Serial.println(connectedDevices);

    // Keep advertising so a second phone can still connect
    BLEDevice::startAdvertising();
  }

  void onDisconnect(BLEServer* pServer) override {
    connectedDevices--;
    if (connectedDevices < 0) connectedDevices = 0;
    Serial.print("Device disconnected. Total: ");
    Serial.println(connectedDevices);
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting BLE StairLight multi-connect...");

  // Display init
  st7735.st7735_init();
  st7735.st7735_fill_screen(ST7735_BLACK);
  st7735.st7735_write_str(0, 0, "StairLight", Font_11x18, ST7735_WHITE, ST7735_BLACK);

  // BLE init
  BLEDevice::init("StairLight");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // ===== Service 1: StairLight (notify + color) =====
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Notify characteristic
  pNotifyChar = pService->createCharacteristic(
    CHAR_NOTIFY_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pNotifyChar->addDescriptor(new BLE2902());
  pNotifyChar->setValue("0");

  // Color characteristic
  colorChar = pService->createCharacteristic(
    CHAR_COLOR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
  );
  colorChar->setCallbacks(new ColorCallbacks());
  colorChar->setValue("#000000");

  pService->start();

  // ===== Service 2: positionService (radar XY) =====
  BLEService *positionService = pServer->createService(POSITION_SERVICE_UUID);

  radarChar = positionService->createCharacteristic(
    CHAR_RADAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  radarChar->setCallbacks(new RadarCallbacks());

  positionService->start();

  // ===== Advertising =====
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->addServiceUUID(POSITION_SERVICE_UUID);  // NEW: adverteer ook positionService
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Advertising 'StairLight' with Stair service + positionService");
}


void loop() {
  // Notify counter to all connected, subscribed clients
  if (connectedDevices > 0) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)counterValue);

    pNotifyChar->setValue((uint8_t*)buffer, strlen(buffer));
    pNotifyChar->notify();  // goes to ALL subscribed phones

    Serial.print("Notified value: ");
    Serial.print(buffer);
    Serial.print(" to ");
    Serial.print(connectedDevices);
    Serial.println(" device(s)");

    counterValue++;
    delay(500);  // don't spam too fast
  } else {
    delay(200);
  }
}
