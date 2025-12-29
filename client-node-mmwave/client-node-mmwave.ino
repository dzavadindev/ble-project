#include <Arduino.h>
#include "BLEDevice.h"
#include <RadarSensor.h>

// ================= RADAR (via RadarSensor lib) =================
#define RADAR_RX_PIN 18
#define RADAR_TX_PIN 17
#define RADAR_UART   1
#define RADAR_BAUD   256000

RadarSensor radar(RADAR_RX_PIN, RADAR_TX_PIN, RADAR_UART);

// If you want to also notify "no target", set to 1
#define SEND_NO_TARGET 1

// ================= BLE CLIENT =================
static BLEUUID serviceUUID("beb5483e-36e1-4688-b7f5-ea07361b26a7");
static BLEUUID charUUID   ("beb5483e-36e1-4688-b7f5-ea07361b26a8");

static boolean connected = false;
static boolean doConnect = false;
static boolean doScan    = false;

static BLERemoteCharacteristic *pRemoteCharacteristic = nullptr;
static BLEAdvertisedDevice     *myDevice = nullptr;

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) override {}
  void onDisconnect(BLEClient *pclient) override {
    connected = false;
    Serial.println("BLE disconnected");
  }
};

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (advertisedDevice.haveServiceUUID() &&
        advertisedDevice.isAdvertisingService(serviceUUID)) {

      Serial.print("Found BLE server: ");
      Serial.println(advertisedDevice.toString().c_str());

      BLEDevice::getScan()->stop();
      myDevice  = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan    = true;
    }
  }
};

bool connectToServer() {
  if (!myDevice) return false;

  Serial.print("Connecting to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient *pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  pClient->connect(myDevice);

  pClient->setMTU(517);

  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (!pRemoteService) {
    Serial.println("Service not found");
    pClient->disconnect();
    return false;
  }

  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (!pRemoteCharacteristic) {
    Serial.println("Characteristic not found");
    pClient->disconnect();
    return false;
  }

  connected = true;
  Serial.println("BLE connected + characteristic ready");
  return true;
}

// Payload: [x_lo, x_hi, y_lo, y_hi] (little-endian int16)
void sendXY(int16_t x_mm, int16_t y_mm) {
  if (!connected || pRemoteCharacteristic == nullptr) return;

  uint8_t payload[4];
  payload[0] = (uint8_t)(x_mm & 0xFF);
  payload[1] = (uint8_t)((x_mm >> 8) & 0xFF);
  payload[2] = (uint8_t)(y_mm & 0xFF);
  payload[3] = (uint8_t)((y_mm >> 8) & 0xFF);

  pRemoteCharacteristic->writeValue(payload, sizeof(payload), false);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("RadarSensor X+Y -> BLE client");

  // Start radar via your library
  radar.begin(RADAR_BAUD);

  // BLE init + scan
  BLEDevice::init("RadarNode");

  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}

void loop() {
  // --- BLE state machine ---
  
  if (doConnect) {
    if (!connectToServer()) Serial.println("BLE connect failed");
    doConnect = false;
  }
  if (!connected && doScan) {
    BLEDevice::getScan()->start(0);
  }

  // --- Radar update via library ---
  if (!radar.update()) return;

  RadarTarget tgt = radar.getTarget();

  bool noTarget = (tgt.detected);
  Serial.println(noTarget);

  if (noTarget) {
    sendXY(0, 0);
  }

  // Send raw mm values (assuming tgt.x/tgt.y are mm)
  sendXY((int16_t)tgt.x, (int16_t)tgt.y);

  // Optional debug:
  Serial.printf("x=%d y=%d\n", (int)tgt.x, (int)tgt.y);
}
