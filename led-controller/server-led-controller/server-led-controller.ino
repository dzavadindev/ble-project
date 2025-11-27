#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "HT_st7735.h"
#include "Arduino.h"

HT_st7735 st7735;

// ===== Service & characteristic UUIDs =====
#define SERVICE_UUID       "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_NOTIFY_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a8"   // counter notify
#define CHAR_COLOR_UUID    "4fafc201-1fb5-459e-8fcc-c5c9c3319101"   // hex color R/W

BLEServer*         pServer       = nullptr;
BLECharacteristic* pNotifyChar   = nullptr;  // counter notify
BLECharacteristic* colorChar     = nullptr;  // hex color R/W

volatile int  connectedDevices = 0;
uint32_t      counterValue     = 0;

// ===== Color characteristic callbacks =====
class ColorCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    // FIX: use Arduino String, not std::string
    String hex = pCharacteristic->getValue();  
    hex.trim();

    Serial.print("Raw color from BLE: ");
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

  // ===== Init display =====
  st7735.st7735_init();
  st7735.st7735_fill_screen(ST7735_BLACK);
  st7735.st7735_write_str(0, 0, "StairLight", Font_11x18, ST7735_WHITE, ST7735_BLACK);

  // ===== Init BLE device =====
  BLEDevice::init("StairLight");  // name seen on phones

  // Create BLE server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create BLE service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // --- Notify characteristic (counter to all clients) ---
  pNotifyChar = pService->createCharacteristic(
    CHAR_NOTIFY_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pNotifyChar->addDescriptor(new BLE2902());  // needed for notifications
  pNotifyChar->setValue("0");

  // --- Color characteristic (hex color R/W) ---
  colorChar = pService->createCharacteristic(
    CHAR_COLOR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
  );
  colorChar->setCallbacks(new ColorCallbacks());
  colorChar->setValue("#000000");  // initial value

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Advertising as 'StairLight' with counter+color characteristics");
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
