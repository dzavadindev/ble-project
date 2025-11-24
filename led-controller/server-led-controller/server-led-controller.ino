#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "HT_st7735.h"
#include "Arduino.h"

HT_st7735 st7735;

// Service & characteristic UUIDs
#define SERVICE_UUID     "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_COLOR_UUID   "4fafc201-1fb5-459e-8fcc-c5c9c3319101"

BLECharacteristic* colorChar;  

class ColorCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String hex = pCharacteristic->getValue();  // Arduino String
    hex.trim();

    Serial.print("Raw color from BLE: ");
    Serial.println(hex);

    if (hex.length() == 0) return;


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

 
    uint16_t color565 =
        ((r & 0xF8) << 8) |   // top 5 bits of red
        ((g & 0xFC) << 3) |   // top 6 bits of green
        (b >> 3);             // top 5 bits of blue

   
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

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE StairLight...");


  st7735.st7735_init();
  st7735.s
  st7735.st7735_fill_screen(ST7735_BLACK);
  st7735.st7735_write_str(0, 0, "StairLight");

  
  BLEDevice::init("StairLight");  

  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);


  colorChar = pService->createCharacteristic(
      CHAR_COLOR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );

  colorChar->setCallbacks(new ColorCallbacks());


  colorChar->setValue("Hello from StairLight");

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Advertising as 'StairLight' with 1 RW characteristic");
}

void loop() {
  // nothing needed here for BLE; all happens via callbacks
  delay(100);
}
