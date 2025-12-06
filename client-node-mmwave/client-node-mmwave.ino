#include <Arduino.h>
#include "BLEDevice.h"


#define RADAR_RX_PIN 18        
#define RADAR_TX_PIN 17        
#define RADAR_BAUD   256000

HardwareSerial Radar(1);


const uint8_t FRAME_HEADER[4] = {0xAA, 0xFF, 0x03, 0x00};
const uint8_t FRAME_TAIL[2]   = {0x55, 0xCC};
const size_t  FRAME_LEN       = 30;   // 4 + (3 * 8) + 2

// Single-target mode command (from Ai-Thinker docs)
uint8_t Single_Target_Detection_CMD[12] = {
  0xFD, 0xFC, 0xFB, 0xFA,
  0x02, 0x00, 0x80, 0x00,   // 0x80 = single-target
  0x04, 0x03, 0x02, 0x01
};

uint8_t rxBuf[64];
size_t  rxCount = 0;

// ========= BLE client setup =========


static BLEUUID serviceUUID("beb5483e-36e1-4688-b7f5-ea07361b2600");  // POSITION_SERVICE_UUID
static BLEUUID charUUID   ("beb5483e-36e1-4688-b7f5-ea07361b2601");  // CHAR_RADAR_UUID

static boolean doConnect = false;
static boolean connected = false;
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
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    if (advertisedDevice.haveServiceUUID() &&
        advertisedDevice.isAdvertisingService(serviceUUID)) {

      BLEDevice::getScan()->stop();
      myDevice  = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan    = true;

      Serial.println("Found target BLE server, stopping scan");
    }
  }
};

bool connectToServer() {
  if (!myDevice) return false;

  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient *pClient = BLEDevice::createClient();
  Serial.println(" - Created BLE client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the BLE server (the one advertising our service)
  pClient->connect(myDevice);
  Serial.println(" - Connected to BLE server");

  pClient->setMTU(517);  // request max MTU

  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found BLE service");

  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found BLE characteristic (writable)");

  connected = true;
  return true;
}

// ========= RD-03D decode helper =========
// RD-03D uses: bit15 = sign (1 = +, 0 = −), rest = |value|
int16_t decodeSigned15(uint16_t raw)
{
  bool positive = raw & 0x8000;
  uint16_t mag  = raw & 0x7FFF;
  int16_t value = (int16_t)mag;
  return positive ? value : -value;
}

// ========= BLE: send raw radar reading =========
// Binary payload: [x_lo, x_hi, y_lo, y_hi, v_lo, v_hi, pixel_lo, pixel_hi]
void sendRadarRaw(int16_t x_mm, int16_t y_mm)
{
  if (!connected || pRemoteCharacteristic == nullptr) return;

  uint8_t payload[4];
  payload[0] = x_mm & 0xFF;
  payload[1] = (x_mm >> 8) & 0xFF;
  payload[2] = y_mm & 0xFF;
  payload[3] = (y_mm >> 8) & 0xFF;

  pRemoteCharacteristic->writeValue(payload, sizeof(payload), false);
}


// ========= parsing one frame (just to raw numbers) =========
bool parseFrame(const uint8_t *buf, size_t len)
{
  if (len < FRAME_LEN) return false;

  // header check
  if (buf[0] != FRAME_HEADER[0] ||
      buf[1] != FRAME_HEADER[1] ||
      buf[2] != FRAME_HEADER[2] ||
      buf[3] != FRAME_HEADER[3]) {
    return false;
  }

  // tail check
  if (buf[28] != FRAME_TAIL[0] || buf[29] != FRAME_TAIL[1]) {
    return false;
  }

  // ---- Target 1 data (bytes 4–11) ----
  uint16_t rawX      = (uint16_t)buf[4]  | ((uint16_t)buf[5]  << 8);
  uint16_t rawY      = (uint16_t)buf[6]  | ((uint16_t)buf[7]  << 8);
  uint16_t rawSpeed  = (uint16_t)buf[8]  | ((uint16_t)buf[9]  << 8);
  uint16_t rawPixelD = (uint16_t)buf[10] | ((uint16_t)buf[11] << 8);

  int16_t x_mm    = decodeSigned15(rawX);      // mm
  int16_t y_mm    = decodeSigned15(rawY);      // mm
  int16_t v_cms   = decodeSigned15(rawSpeed);  // cm/s
  uint16_t dGate  = rawPixelD;                 // mm “pixel distance value”

  // If all zero → no target in this slot
  if (x_mm == 0 && y_mm == 0 && v_cms == 0 && dGate == 0) {
    // You can still send "no target" if you want; for now we skip
    Serial.println("No target");
    return true;
  }

  float dist_mm   = sqrtf((float)x_mm * x_mm + (float)y_mm * y_mm);
  float dist_cm   = dist_mm / 10.0f;
  float angle_deg = atan2f((float)y_mm, (float)x_mm) * 180.0f / PI;

  Serial.print("Target 1 -> ");
  Serial.print("X: ");      Serial.print(x_mm);     Serial.print(" mm,  ");
  Serial.print("Y: ");      Serial.print(y_mm);     Serial.print(" mm,  ");
  Serial.print("Dist: ");   Serial.print(dist_cm);  Serial.print(" cm,  ");
  Serial.print("Angle: ");  Serial.print(angle_deg);Serial.print(" deg,  ");
  Serial.print("Speed: ");  Serial.print(v_cms);    Serial.print(" cm/s,  ");
  Serial.print("Pixel: ");  Serial.print(dGate);    Serial.println(" mm");

  // ---- send raw to BLE server ----
  sendRadarRaw(x_mm, y_mm, v_cms, dGate);

  return true;
}

// ========= send mode command =========
void sendSingleTargetCmd()
{
  Serial.println("Setting radar to single-target mode...");
  Radar.write(Single_Target_Detection_CMD, sizeof(Single_Target_Detection_CMD));
  Radar.flush();
}

// ========= Arduino setup =========
void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("RD-03D raw → BLE client");

  // Radar UART
  Radar.begin(RADAR_BAUD, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  Radar.setRxBufferSize(256);
  delay(100);
  sendSingleTargetCmd();

  // BLE init
  Serial.println("Starting BLE client...");
  BLEDevice::init("RadarNode");

  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}

// ========= Arduino loop =========
void loop()
{
  // --- BLE state machine ---
  if (doConnect) {
    if (connectToServer()) {
      Serial.println("BLE: Connected to server");
    } else {
      Serial.println("BLE: Failed to connect to server");
    }
    doConnect = false;
  }

  if (!connected && doScan) {
    // Restart scan after disconnect
    BLEDevice::getScan()->start(0);
  }

  // --- Radar reading ---
  while (Radar.available()) {
    uint8_t b = Radar.read();

    // simple resync on header first byte
    if (rxCount == 0 && b != FRAME_HEADER[0]) {
      continue;
    }

    rxBuf[rxCount++] = b;
    if (rxCount >= sizeof(rxBuf)) {
      rxCount = 0; // overflow safety
      continue;
    }

    if (rxCount >= FRAME_LEN) {
      if (parseFrame(rxBuf, FRAME_LEN)) {
        rxCount = 0; // good frame → reset
      } else {
        // shift by one byte to search for the next header
        memmove(rxBuf, rxBuf + 1, rxCount - 1);
        rxCount -= 1;
      }
    }
  }

  // small pause so we don't spin crazy when no data:
  delay(5);
}
