#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "HT_st7735.h"
#include "Arduino.h"

HT_st7735 st7735;

// ===== Service & characteristic UUIDs =====
#define CONTROL_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"  // CONTROL SERVICE
#define CHAR_COLOR_UUID "4fafc201-1fb5-459e-8fcc-c5c9c3319101"       // u16x3 hex color (rgb) R/W
#define CHAR_BRIGHTNESS_UUID "41db7a25-59f1-405e-b349-13afcfb5c806"  // u8 brighness R/W

#define POSITION_SERVICE_UUID "beb5483e-36e1-4688-b7f5-ea07361b2600"  // POSITION SERVICE
#define CHAR_POSITION_UUID "beb5483e-36e1-4688-b7f5-ea07361b2601"     // i16 position (xy) W/N


BLEServer* pServer = nullptr;

BLECharacteristic* pColorChar = nullptr;
BLECharacteristic* pBrightnessChar = nullptr;

BLECharacteristic* pPositionChar = nullptr;

volatile int connectedDevices = 0;
uint32_t counterValue = 0;


// ===== Control->Color characteristic callbacks =====
class ColorCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String hex = pCharacteristic->getValue().c_str();
    hex.trim();

    Serial.print("Raw color from BLE: ");
    Serial.println(hex);

    if (hex.length() != 6 || hex.length() == 0) {
      Serial.println("Invalid color length, expected 6 hex chars");
      return;
    }

    if (hex[0] == '#') hex.remove(0, 1);  // get rid of the leading #

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

    // convert 16-bit int to an rgb565 color
    uint16_t color565 =
      ((r & 0xF8) << 8) |  // top 5 bits of red
      ((g & 0xFC) << 3) |  // top 6 bits of green
      (b >> 3);            // top 5 bits of blue

    // fill screen with color + show text
    st7735.st7735_fill_screen(color565);
    st7735.st7735_write_str(
      0, 0,
      hex.c_str(),
      Font_11x18,
      ST7735_WHITE,  // text color
      color565       // background color
    );
  }
};


// ===== Positioning->Position characteristic callbacks =====
class RadarCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String value = pCharacteristic->getValue();

    int len = value.length();
    if (len < 4) {
      Serial.printf("Radar write too short: %d\n", len);
      return;
    }

    // treat underlying buffer as raw bytes
    const uint8_t* data = (const uint8_t*)value.c_str();

    // little endian
    int16_t x_mm = (int16_t)(data[0] | (data[1] << 8));
    int16_t y_mm = (int16_t)(data[2] | (data[3] << 8));

    Serial.print("Radar XY from node (pPositionService): x=");
    Serial.print(x_mm);
    Serial.print(" mm, y=");
    Serial.print(y_mm);
    Serial.println(" mm");

    // TODO: here later:
    //  - mapToLedIndex(x_mm, y_mm)
    //  - LED animations
  }
};


// ===== Control->Brightness characteristic callbacks =====
class BrightnessCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String value = pCharacteristic->getValue();
    if (value.length() != 1) {
      Serial.println("Data packet is bigger then expected (1 byte)");
      return;
    }

    const uint8_t* data = (const uint8_t*)value.c_str();

    Serial.print("Changed the brightness to ");
    Serial.println(data[0]);
  }
};


// ===== Server connect/disconnect callbacks (multi-connect) =====
class ServerCallbacks : public BLEServerCallbacks {
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
  pServer->setCallbacks(new ServerCallbacks());

  // Service 1 ================= Control service
  BLEService* pControlService = pServer->createService(CONTROL_SERVICE_UUID);

  // Notify characteristic
  // pNotifyChar = pControlService->createCharacteristic(
  //   CHAR_NOTIFY_UUID,
  //   BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  // pNotifyChar->addDescriptor(new BLE2902());
  // pNotifyChar->setValue("0");

  // Color characteristic
  pColorChar = pControlService->createCharacteristic(
    CHAR_COLOR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pColorChar->setCallbacks(new ColorCallbacks());
  pColorChar->setValue("0");

  // Brightness characteristic
  pBrightnessChar = pControlService->createCharacteristic(
    CHAR_BRIGHTNESS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pBrightnessChar->setCallbacks(new BrightnessCallbacks());
  pBrightnessChar->setValue("0");

  pControlService->start();

  // Service 2 ================= Positioning service
  BLEService* pPositionService = pServer->createService(POSITION_SERVICE_UUID);

  pPositionChar = pPositionService->createCharacteristic(
    CHAR_POSITION_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pPositionChar->setCallbacks(new RadarCallbacks());

  pPositionService->start();

  // ===== Advertising =====
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(CONTROL_SERVICE_UUID);
  pAdvertising->addServiceUUID(POSITION_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Advertising 'StairLight' with Stair service + pPositionService");
}


void loop() {
}
