#pragma once
#include <Arduino.h>
#include <FastLED.h>

class StairChaser {
public:
  StairChaser(CRGB* leds,
              uint16_t numLeds,
              float maxYcm,
              uint16_t trackStartLed,
              uint16_t trackEndLed);

  void begin(uint8_t brightness = 80);
  void reset();

  // Straight segment (track) on the strip
  void setRange(uint16_t trackStartLed, uint16_t trackEndLed);

  // Physical strip ends (whole path) used for entry + finish
  void setPhysicalEnds(uint16_t topEndLed, uint16_t bottomEndLed);

  // Visual settings
  void setColors(const CRGB& fg, const CRGB& bg);
  void setTailLen(uint16_t tailLen);

  // Motion settings
  void setSpeedLedsPerSecond(float ledsPerSec);     // e.g. 90
  void setMaxStepsPerUpdate(uint8_t maxSteps);      // safety against big jumps if loop stalls
  void setLostTimeoutMs(uint32_t ms);               // e.g. 250-400ms
  void setFadeOutMs(uint32_t ms);                   // e.g. 400-700ms
  bool getActiveRange(uint16_t& startLed, uint16_t& endLed) const;

  // Direction-based lead offsets (in LEDs)
  // walking DOWN (top->bottom): +leadDown
  // walking UP   (bottom->top): -leadUp
  void setLeadOffsets(int16_t leadDown, int16_t leadUp);

  // Allow for top-of-staircase or bottom-of-staircase setups
  void setInvertY(bool inv);

  // Direction detection sensitivity (cm)
  void setDirectionDeadbandCm(float cm);

  // Main update function
  void update(bool hasTarget, float y_cm);

private:
  enum State : uint8_t { IDLE, ENTERING, TRACKING, FINISHING, FADING };
  enum Dir   : uint8_t { DIR_UNKNOWN, DIR_DOWN, DIR_UP };

  CRGB* _leds = nullptr;
  uint16_t _numLeds = 0;

  float _maxYcm = 100.0f;
  bool  _invertY = false;

  // Track range (straight segment)
  uint16_t _trackMin = 0;
  uint16_t _trackMax = 0;

  // Physical path ends
  uint16_t _physTop = 0;
  uint16_t _physBottom = 0;

  // Appearance
  CRGB _fg = CRGB::White;
  CRGB _bg = CRGB::Black;
  uint16_t _tailLen = 16;

  // Motion
  float _ledsPerSec = 90.0f;
  uint8_t _maxStepsPerUpdate = 6;
  uint32_t _lostTimeoutMs = 300;
  uint32_t _fadeOutMs = 450;

  // Lead
  int16_t _leadDown = 0;
  int16_t _leadUp = 0;

  // Direction detection
  float _dirDeadbandCm = 3.0f;  // ignore small jitter

  // Runtime
  State _state = IDLE;
  Dir   _dir = DIR_UNKNOWN;

  int16_t _head = 0;      // current head LED (integer to avoid skipping)
  int16_t _goal = 0;      // goal LED
  int8_t  _moveSign = 0;  // last movement direction (-1, +1)

  float _lastYcm = 0.0f;
  bool  _haveLastY = false;

  // Direction stabilization
  Dir _dirCandidate = DIR_UNKNOWN;
  uint8_t _dirCandidateCount = 0;
  uint8_t _dirConfirmCount = 3;      // require 3 consistent updates before switching
  uint32_t _spawnMs = 0;
  uint32_t _dirLockMs = 800;         // lock direction for first 800ms after spawn

  uint32_t _lastSeenMs = 0;
  uint32_t _lastStepMs = 0;
  uint32_t _fadeStartMs = 0;

  // active range
  uint16_t _activeMin = 0;
  uint16_t _activeMax = 0;
  bool     _activeValid = false;

  // helpers
  uint16_t clampU16(int32_t v, uint16_t lo, uint16_t hi) const;
  int16_t  clampI16(int32_t v, int16_t lo, int16_t hi) const;

  int16_t mapYToTrackLed(float y_cm) const;
  int16_t applyLead(int16_t baseTrackLed) const;

  void decideDirection(float y_cm);
  void updateDirectionTrend(float y_cm);

  void setGoal(int16_t g);
  bool stepTowardGoal(uint8_t stepsAllowed);
  void render(float fade01 = 1.0f);

  int8_t signOf(int32_t x) const { return (x > 0) - (x < 0); }
};
