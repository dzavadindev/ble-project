#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>

// ================== Radar UART (ESP32-S3 Heltec Wireless Tracker) ==================
#define RADAR_RX_PIN 18      // RD-03D TX -> this pin (ESP RX)
#define RADAR_TX_PIN 17      // RD-03D RX <- this pin (ESP TX)
#define RADAR_BAUD   256000

HardwareSerial Radar(1);

// ================== RD-03D frame parsing ==================
const uint8_t FRAME_HEADER[4] = {0xAA, 0xFF, 0x03, 0x00};
const uint8_t FRAME_TAIL[2]   = {0x55, 0xCC};
const size_t  FRAME_LEN       = 30;  // 4 + (3 * 8) + 2

uint8_t rxBuf[64];
size_t  rxCount = 0;

bool          radarConnected         = false;
unsigned long lastRadarFrameMs       = 0;
const unsigned long RADAR_TIMEOUT_MS = 1000; // 1s no frames => "disconnected"

// RD-03D signed 15-bit pattern: bit15 = sign (1 = +, 0 = −)
int16_t decodeSigned15(uint16_t raw) {
  bool positive = (raw & 0x8000u) != 0;
  uint16_t mag  = raw & 0x7FFFu;
  int16_t value = (int16_t)mag;
  return positive ? value : -value;
}

// ================== BLE client (connect to server) ==================

static const char *BLE_CLIENT_NAME = "RadarNodeClient";

#define POSITION_SERVICE_UUID   "beb5483e-36e1-4688-b7f5-ea07361b2600"
#define POSITION_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b2601"

BLEScan                 *pScan         = nullptr;
BLEClient               *pClient       = nullptr;
BLERemoteCharacteristic *pRemoteChar   = nullptr;
bool                     clientConnected = false;

// ---- client callbacks to detect disconnects ----
class MyClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient* client) override {
    Serial.println("BLE client callback: connected to server.");
    clientConnected = true;
  }

  void onDisconnect(BLEClient* client) override {
    Serial.println("BLE client callback: disconnected from server.");
    clientConnected = false;
    pClient        = nullptr;
    pRemoteChar    = nullptr;
  }
};

MyClientCallbacks g_clientCallbacks;

// Try to connect to a server that advertises POSITION_SERVICE_UUID
bool connectToServer() {
  if (clientConnected) return true;

  Serial.println("Scanning for BLE server...");

  // Depending on your BLE library, start() returns BLEScanResults* or BLEScanResults.
  // Your current setup uses a pointer, so we keep that:
  BLEScanResults* results = pScan->start(3 /* seconds */, false);
  int foundCount = results->getCount();

  for (int i = 0; i < foundCount; ++i) {
    BLEAdvertisedDevice dev = results->getDevice(i);

    // Only consider devices that advertise our service UUID
    if (!dev.haveServiceUUID()) continue;
    if (!dev.isAdvertisingService(BLEUUID(POSITION_SERVICE_UUID))) continue;

    Serial.print("Found target server: ");
    Serial.println(dev.toString().c_str());

    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(&g_clientCallbacks);
    Serial.println("Created BLE client, connecting...");

    if (!pClient->connect(&dev)) {
      Serial.println("Failed to connect to server.");
      pClient = nullptr;
      continue; // try next result
    }

    Serial.println("Connected to BLE server (no services yet).");

    BLERemoteService *pService = pClient->getService(POSITION_SERVICE_UUID);
    if (pService == nullptr) {
      Serial.println("Failed to find remote service.");
      pClient->disconnect();
      pClient = nullptr;
      continue;
    }

    pRemoteChar = pService->getCharacteristic(POSITION_CHAR_UUID);
    if (pRemoteChar == nullptr) {
      Serial.println("Failed to find remote characteristic.");
      pClient->disconnect();
      pClient = nullptr;
      continue;
    }

    Serial.println("Found remote position characteristic.");

    // We let the callback set clientConnected=true too, but we set it here as well
    clientConnected = true;

    pScan->clearResults();
    return true;
  }

  pScan->clearResults();
  Serial.println("No suitable BLE server found.");
  return false;
}

// ================== Parse one radar frame & send x,y to BLE server ==================

bool parseFrame(const uint8_t *buf, size_t len) {
  if (len < FRAME_LEN) return false;

  // Header check
  if (buf[0] != FRAME_HEADER[0] ||
      buf[1] != FRAME_HEADER[1] ||
      buf[2] != FRAME_HEADER[2] ||
      buf[3] != FRAME_HEADER[3]) {
    return false;
  }

  // Tail check
  if (buf[28] != FRAME_TAIL[0] ||
      buf[29] != FRAME_TAIL[1]) {
    return false;
  }

  // Valid frame received from radar
  radarConnected   = true;
  lastRadarFrameMs = millis();

  // ---- Target 1 data (bytes 4–11) ----
  uint16_t rawX      = (uint16_t)buf[4]  | ((uint16_t)buf[5]  << 8);
  uint16_t rawY      = (uint16_t)buf[6]  | ((uint16_t)buf[7]  << 8);
  uint16_t rawSpeed  = (uint16_t)buf[8]  | ((uint16_t)buf[9]  << 8);
  uint16_t rawPixelD = (uint16_t)buf[10] | ((uint16_t)buf[11] << 8);

  int16_t x_mm   = decodeSigned15(rawX);
  int16_t y_mm   = decodeSigned15(rawY);
  int16_t v_cms  = decodeSigned15(rawSpeed);
  uint16_t dGate = rawPixelD;

  // All zero => no target in this slot
  if (x_mm == 0 && y_mm == 0 && v_cms == 0 && dGate == 0) {
    // No target, nothing to send
    return true;
  }

  // Debug print
  Serial.print("Radar target -> X: ");
  Serial.print(x_mm);
  Serial.print(" mm, Y: ");
  Serial.print(y_mm);
  Serial.print(" mm, Pixel: ");
  Serial.print(dGate);
  Serial.println(" mm");

  // ==== Send 4-byte payload over BLE if client is connected ====
  if (clientConnected && pClient && pClient->isConnected() && pRemoteChar != nullptr) {
    uint8_t payload[4];
    payload[0] = (uint8_t)(x_mm & 0xFF);
    payload[1] = (uint8_t)((x_mm >> 8) & 0xFF);
    payload[2] = (uint8_t)(y_mm & 0xFF);
    payload[3] = (uint8_t)((y_mm >> 8) & 0xFF);

    pRemoteChar->writeValue(payload, sizeof(payload), false);
    Serial.println("BLE write (4 bytes x,y).");
  } else {
    Serial.println("Not connected to BLE server, skipping BLE write.");
  }

  return true;
}

// ================== Setup / Loop ==================

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && (millis() - start < 3000)) {
    ; // wait up to ~3s for USB serial (optional)
  }

  Serial.println();
  Serial.println("RD-03D -> BLE client (x,y as 4 bytes)");

  // --- Radar UART ---
  Radar.begin(RADAR_BAUD, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  Radar.setRxBufferSize(256);

  // --- BLE init as client ---
  BLEDevice::init(BLE_CLIENT_NAME);

  pScan = BLEDevice::getScan();
  pScan->setActiveScan(true);   // better results, slightly more power

  // First connection attempt
  connectToServer();
}

void loop() {
  unsigned long now = millis();

  // ---- Radar connection timeout check ----
  if (radarConnected && (now - lastRadarFrameMs > RADAR_TIMEOUT_MS)) {
    radarConnected = false;
    Serial.println("Radar disconnected (no frames within timeout).");
  }

  // ---- Check BLE connection, reconnect if needed ----
  if (clientConnected) {
    if (!pClient || !pClient->isConnected()) {
      Serial.println("BLE server disconnected (isConnected check), will rescan.");
      clientConnected = false;
      pClient        = nullptr;
      pRemoteChar    = nullptr;
    }
  } else {
    // Not connected -> try to find/connect to server
    connectToServer();
  }

  // ---- Read from radar UART, assemble frames ----
  while (Radar.available()) {
    uint8_t b = Radar.read();

    // Simple resync on header first byte
    if (rxCount == 0 && b != FRAME_HEADER[0]) {
      continue;
    }

    rxBuf[rxCount++] = b;
    if (rxCount >= sizeof(rxBuf)) {
      rxCount = 0;  // overflow safety
      continue;
    }

    if (rxCount >= FRAME_LEN) {
      if (parseFrame(rxBuf, FRAME_LEN)) {
        // good frame -> reset
        rxCount = 0;
      } else {
        // shift by one byte to search for header
        memmove(rxBuf, rxBuf + 1, rxCount - 1);
        rxCount -= 1;
      }
    }
  }
}
