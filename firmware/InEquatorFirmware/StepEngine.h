#pragma once

#include <Arduino.h>

// Timer-driven STEP/DIR pulse engine for ESP8266.
//
// - Runs on hw_timer1 (NMI), single-axis, single instance (AxisEngine).
// - Rate is signed steps/s; the sign selects DIR.
// - Fractional period accumulation (1/65536 tick remainder) keeps the
//   long-term average rate exact, which matters for sidereal tracking
//   (3.5653 steps/s at 1/16 microstep, 200-step motor, 96:1 worm).
// - Auto-selects TIM_DIV16 (5 MHz, >= 1 steps/s) or TIM_DIV256
//   (312.5 kHz, 0.05 .. 1 steps/s).
class StepEngine {
 public:
  void begin(uint8_t stepPin, uint8_t dirPin);

  // Signed steps/s. Clamped to [-10000, 10000]. Rates below 0.05 stop.
  void setRate(float stepsPerSec);
  void stop();
  float getRate() const { return _mrate / 1000.0f; }
  bool isRunning() const { return _mrate != 0; }

  int32_t getPosition() const { return _position; }
  void setPosition(int32_t position) {
    noInterrupts();
    _position = position;
    interrupts();
  }
  uint32_t getPulses() const { return _pulses; }

 private:
  static void timerIsr();

  uint8_t _stepPin = 255;
  uint8_t _dirPin = 255;
  uint32_t _stepMask = 0;
  uint32_t _dirMask = 0;

  volatile int32_t _mrate = 0;       // milli steps/s, signed
  volatile int8_t _dir = 1;
  volatile int32_t _position = 0;
  volatile uint32_t _pulses = 0;
  volatile uint8_t _phase = 0;       // 0 = STEP low, 1 = STEP high
  volatile uint32_t _periodTicks = 0;
  volatile uint32_t _periodRem = 0;  // per-step remainder, 1/65536 tick
  volatile uint32_t _fracAccum = 0;
  volatile uint32_t _pulseTicks = 10;
  uint32_t _clk = 5000000UL;         // timer clock Hz for current divider
  uint8_t _divider = 1;              // TIM_DIV16
};

extern StepEngine AxisEngine;
