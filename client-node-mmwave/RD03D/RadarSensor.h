#ifndef RADARSENSOR_H
#define RADARSENSOR_H

#include <Arduino.h>
#include <HardwareSerial.h>

typedef struct RadarTarget {
    float distance;  // mm
    float angle;     // radians
    float speed;     // cm/s
    int16_t x;       // mm
    int16_t y;       // mm
    bool detected;
} RadarTarget;

class RadarSensor {
public:
    // uartNum = 1 → use UART1 (good default on ESP32)
    RadarSensor(uint8_t rxPin, uint8_t txPin, uint8_t uartNum = 1);

    void begin(unsigned long baud = 256000);
    bool update();
    RadarTarget getTarget(); // No index, only first target

private:
    HardwareSerial radarSerial;
    uint8_t _rxPin;
    uint8_t _txPin;

    RadarTarget target;  // single target
    bool parseData(const uint8_t* buffer, size_t len);
};

#endif
