#include "StairChaser.h"

StairChaser::StairChaser(CRGB* leds,
                         uint16_t numLeds,
                         float maxYcm,
                         uint16_t trackStartLed,
                         uint16_t trackEndLed)
: _leds(leds), _numLeds(numLeds), _maxYcm(maxYcm)
{
  if (_numLeds == 0) return;

  setRange(trackStartLed, trackEndLed);
  setPhysicalEnds(0, _numLeds - 1);

  _head = (int16_t)_physTop;
  _goal = _head;
  _lastStepMs = millis();
  _lastSeenMs = millis();
}

void StairChaser::begin(uint8_t brightness) {
  FastLED.setBrightness(brightness);
  reset();
}

void StairChaser::reset() {
  _state = IDLE;
  _dir = DIR_UNKNOWN;
  _haveLastY = false;
  _moveSign = 0;

  _head = (int16_t)_physTop;
  _goal = _head;

  _lastStepMs = millis();
  _lastSeenMs = millis();
  _fadeStartMs = 0;

  if (_leds && _numLeds) {
    fill_solid(_leds, _numLeds, _bg);
    FastLED.show();
  }
}

void StairChaser::setRange(uint16_t trackStartLed, uint16_t trackEndLed) {
  if (_numLeds == 0) return;

  if (trackStartLed >= _numLeds) trackStartLed = _numLeds - 1;
  if (trackEndLed   >= _numLeds) trackEndLed   = _numLeds - 1;

  uint16_t a = min(trackStartLed, trackEndLed);
  uint16_t b = max(trackStartLed, trackEndLed);
  _trackMin = a;
  _trackMax = b;
}

bool StairChaser::getActiveRange(uint16_t &startLed, uint16_t &endLed) const {
  if (!_activeValid) return false;
  startLed = _activeMin;
  endLed   = _activeMax;
  return true;
}

void StairChaser::setPhysicalEnds(uint16_t topEndLed, uint16_t bottomEndLed) {
  if (_numLeds == 0) return;

  if (topEndLed >= _numLeds) topEndLed = _numLeds - 1;
  if (bottomEndLed >= _numLeds) bottomEndLed = _numLeds - 1;

  _physTop = topEndLed;
  _physBottom = bottomEndLed;
}

void StairChaser::setColors(const CRGB& fg, const CRGB& bg) {
  _fg = fg;
  _bg = bg;
}

void StairChaser::setTailLen(uint16_t tailLen) {
  _tailLen = tailLen;
}

void StairChaser::setSpeedLedsPerSecond(float ledsPerSec) {
  if (ledsPerSec < 1.0f) ledsPerSec = 1.0f;
  _ledsPerSec = ledsPerSec;
}

void StairChaser::setMaxStepsPerUpdate(uint8_t maxSteps) {
  if (maxSteps < 1) maxSteps = 1;
  _maxStepsPerUpdate = maxSteps;
}

void StairChaser::setLostTimeoutMs(uint32_t ms) {
  _lostTimeoutMs = ms;
}

void StairChaser::setFadeOutMs(uint32_t ms) {
  _fadeOutMs = ms;
}

void StairChaser::setLeadOffsets(int16_t leadDown, int16_t leadUp) {
  _leadDown = leadDown;
  _leadUp = leadUp;
}

void StairChaser::setInvertY(bool inv) {
  _invertY = inv;
}

void StairChaser::setDirectionDeadbandCm(float cm) {
  if (cm < 0.0f) cm = 0.0f;
  _dirDeadbandCm = cm;
}

uint16_t StairChaser::clampU16(int32_t v, uint16_t lo, uint16_t hi) const {
  if (v < (int32_t)lo) return lo;
  if (v > (int32_t)hi) return hi;
  return (uint16_t)v;
}

int16_t StairChaser::clampI16(int32_t v, int16_t lo, int16_t hi) const {
  if (v < (int32_t)lo) return lo;
  if (v > (int32_t)hi) return hi;
  return (int16_t)v;
}

int16_t StairChaser::mapYToTrackLed(float y_cm) const {
  if (_maxYcm <= 0.0f) return (int16_t)_trackMin;

  float t = y_cm / _maxYcm;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  if (_invertY) t = 1.0f - t;

  float span = (float)((int32_t)_trackMax - (int32_t)_trackMin);
  int32_t led = (int32_t)_trackMin + (int32_t)lroundf(t * span);

  return (int16_t)clampU16(led, _trackMin, _trackMax);
}

int16_t StairChaser::applyLead(int16_t baseTrackLed) const {
  int32_t desired = baseTrackLed;

  if (_dir == DIR_DOWN) desired += _leadDown;
  else if (_dir == DIR_UP) desired -= _leadUp; // subtract leadUp

  // keep inside track segment
  desired = clampU16(desired, _trackMin, _trackMax);
  return (int16_t)desired;
}

void StairChaser::decideDirection(float y_cm) {
  // Simple first guess: nearer to top half => going DOWN, nearer to bottom half => going UP
  // This matches your wish: "it sees that i am coming from the top".
  if (y_cm <= (_maxYcm * 0.5f)) _dir = DIR_DOWN;
  else _dir = DIR_UP;

  _haveLastY = false; // let trend logic re-initialize cleanly
}

void StairChaser::updateDirectionTrend(float y_cm) {
  uint32_t now = millis();

  // Optional: lock direction briefly after target appears (prevents instant flip)
  if (_spawnMs != 0 && (now - _spawnMs) < _dirLockMs) {
    _lastYcm = y_cm;
    _haveLastY = true;
    return;
  }

  if (!_haveLastY) {
    _lastYcm = y_cm;
    _haveLastY = true;
    return;
  }

  float dy = y_cm - _lastYcm;
  _lastYcm = y_cm;

  if (fabsf(dy) < _dirDeadbandCm) return; // ignore jitter

  Dir cand = (dy > 0.0f) ? DIR_DOWN : DIR_UP;

  // Confirm direction only after N consistent samples
  if (cand != _dirCandidate) {
    _dirCandidate = cand;
    _dirCandidateCount = 1;
  } else {
    if (_dirCandidateCount < 255) _dirCandidateCount++;
    if (_dirCandidateCount >= _dirConfirmCount) {
      _dir = _dirCandidate;
    }
  }
}

void StairChaser::setGoal(int16_t g) {
  if (_numLeds == 0) return;
  if (g < 0) g = 0;
  if (g >= (int16_t)_numLeds) g = (int16_t)_numLeds - 1;
  _goal = g;
}

bool StairChaser::stepTowardGoal(uint8_t /*stepsAllowed*/) {
  if (_head == _goal) return true;

  int32_t diff = (int32_t)_goal - (int32_t)_head;
  int8_t s = signOf(diff);
  _moveSign = s;

  uint32_t now = millis();
  float stepIntervalMsF = 1000.0f / _ledsPerSec;
  uint32_t stepIntervalMs = (uint32_t)max(1.0f, stepIntervalMsF);

  // Only ONE step per update, and DROP backlog (prevents bursts that look like jumps)
  if ((now - _lastStepMs) < stepIntervalMs) {
    return false;
  }
  _lastStepMs = now;

  _head += s;

  // clamp
  if (_head < 0) _head = 0;
  if (_head >= (int16_t)_numLeds) _head = (int16_t)_numLeds - 1;

  // reached / passed
  if ((s > 0 && _head >= _goal) || (s < 0 && _head <= _goal)) {
    _head = _goal;
  }

  return (_head == _goal);
}


void StairChaser::render(float fade01) {
  if (!_leds || !_numLeds) return;

  _activeValid = false;
  _activeMin = _numLeds - 1;
  _activeMax = 0;

  auto markActive = [&](uint16_t i) {
    if (i >= _numLeds) return;
    if (!_activeValid) {
      _activeValid = true;
      _activeMin = i;
      _activeMax = i;
      return;
    }
    if (i < _activeMin) _activeMin = i;
    if (i > _activeMax) _activeMax = i;
  };

  fill_solid(_leds, _numLeds, _bg);

  // Determine tail direction: tail goes "behind" movement.
  int8_t tailSign = 0;
  if (_moveSign != 0) tailSign = -_moveSign;
  else {
    if (_dir == DIR_DOWN) tailSign = -1;
    else if (_dir == DIR_UP) tailSign = +1;
  }

  // Draw head + tail
  for (uint16_t i = 0; i <= _tailLen; i++) {
    int32_t idx = (int32_t)_head + (int32_t)i * tailSign;
    if (idx < 0 || idx >= _numLeds) continue;

    float a = 1.0f - (float)i / (float)max<uint16_t>(1, _tailLen);
    a *= fade01;

    if (a <= 0.0f) continue;   // <- important: don't mark anything when fully faded
    if (a > 1.0f) a = 1.0f;

    CRGB c = _fg;
    c.nscale8_video((uint8_t)lroundf(a * 255.0f));
    _leds[idx] = c;

    markActive((uint16_t)idx); // <- track active LEDs
  }

  FastLED.show();
}


void StairChaser::update(bool hasTarget, float y_cm) {
  if (_numLeds == 0) return;

  uint32_t now = millis();
  // If we reacquire while finishing/fading -> resume tracking immediately
  if (hasTarget && (_state == FINISHING || _state == FADING)) {
    _state = TRACKING;
    _fadeStartMs = 0;
  }

  if (hasTarget) {
    _lastSeenMs = now;

    if (_state == IDLE) {
        decideDirection(y_cm);
        _spawnMs = now;                 // <--- add this
        _dirCandidate = _dir;           // <--- add this
        _dirCandidateCount = 0;

      // Spawn at the physical end based on initial direction
      if (_dir == DIR_DOWN) _head = (int16_t)_physTop;
      else _head = (int16_t)_physBottom;

      _lastStepMs = now;
      _moveSign = 0;

      // First go into the straight segment boundary (entry animation uses all LEDs)
      _state = ENTERING;
      setGoal((_dir == DIR_DOWN) ? (int16_t)_trackMin : (int16_t)_trackMax);

      render(1.0f);
      return;
    }

    // Update direction based on trend (helps when people turn / noise)
    updateDirectionTrend(y_cm);

    // Convert y to track LED and apply lead offset
    int16_t base = mapYToTrackLed(y_cm);
    int16_t desiredTrack = applyLead(base);

    if (_state == ENTERING) {
      // During entry we keep goal as boundary.
      // Once boundary reached, we switch to tracking goal.
      if (stepTowardGoal(_maxStepsPerUpdate)) {
        _state = TRACKING;
        setGoal(desiredTrack);
      }
      render(1.0f);
      return;
    }

    if (_state == TRACKING) {
      setGoal(desiredTrack);
      stepTowardGoal(_maxStepsPerUpdate);
      render(1.0f);
      return;
    }
  }

  // No target
  if (_state == IDLE) {
    // stay idle
    return;
  }

  // Wait a little before deciding it's truly lost (handles dropouts)
  if ((now - _lastSeenMs) <= _lostTimeoutMs) {
    // keep rendering current position
    stepTowardGoal(_maxStepsPerUpdate);
    render(1.0f);
    return;
  }

  // Start/continue finishing to physical end
  if (_state != FINISHING && _state != FADING) {
    _state = FINISHING;
    // If last known direction was down -> finish to bottom. If up -> finish to top.
    if (_dir == DIR_DOWN) setGoal((int16_t)_physBottom);
    else if (_dir == DIR_UP) setGoal((int16_t)_physTop);
    else setGoal((int16_t)_physBottom);

    _fadeStartMs = 0;
  }

  if (_state == FINISHING) {
    if (stepTowardGoal(_maxStepsPerUpdate)) {
      _state = FADING;
      _fadeStartMs = now;
    }
    render(1.0f);
    return;
  }

  if (_state == FADING) {
    float t = 1.0f;
    if (_fadeOutMs > 0) {
      uint32_t dt = now - _fadeStartMs;
      if (dt >= _fadeOutMs) t = 0.0f;
      else t = 1.0f - ((float)dt / (float)_fadeOutMs);
    } else {
      t = 0.0f;
    }

    render(t);

    if (t <= 0.0f) {
      // done
      _spawnMs = 0;
      _dirCandidate = DIR_UNKNOWN;
      _dirCandidateCount = 0;
      _state = IDLE;
      _dir = DIR_UNKNOWN;
      _haveLastY = false;
      _moveSign = 0;
    }
    return;
  }
}
