#include "StepEngine.h"

#include <core_esp8266_timer.h>

StepEngine AxisEngine;

void StepEngine::begin(uint8_t stepPin, uint8_t dirPin) {
  _stepPin = stepPin;
  _dirPin = dirPin;
  _stepMask = 1UL << stepPin;
  _dirMask = 1UL << dirPin;
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  digitalWrite(stepPin, LOW);
  digitalWrite(dirPin, LOW);
  timer1_isr_init();
  timer1_attachInterrupt(StepEngine::timerIsr);
}

// hw_timer1 NMI ISR. Register-only access, safe in NMI context.
// GPIO_OUT_W1TS (set) = 0x60000304, GPIO_OUT_W1TC (clear) = 0x60000308.
ICACHE_RAM_ATTR void StepEngine::timerIsr() {
  StepEngine &e = AxisEngine;
  if (e._phase == 0) {
    if (e._mrate == 0) {
      return;  // rate dropped to zero while timer was still armed
    }
    *(volatile uint32_t *)0x60000304 = e._stepMask;  // STEP rising edge
    e._phase = 1;
    e._dir = (e._mrate > 0) ? 1 : -1;
    e._position += e._dir;
    e._pulses++;
    timer1_write(e._pulseTicks);
  } else {
    *(volatile uint32_t *)0x60000308 = e._stepMask;  // STEP falling edge
    e._phase = 0;
    if (e._mrate == 0) {
      return;
    }
    uint32_t period = e._periodTicks;
    uint32_t acc = e._fracAccum + e._periodRem;
    if (acc >= 65536UL) {
      acc -= 65536UL;
      period += 1;
    }
    e._fracAccum = acc;
    timer1_write(period);
  }
}

void StepEngine::setRate(float rate) {
  if (rate > 10000.0f) {
    rate = 10000.0f;
  }
  if (rate < -10000.0f) {
    rate = -10000.0f;
  }
  if (fabsf(rate) < 0.05f) {
    stop();
    return;
  }

  uint32_t clk;
  uint8_t divider;
  if (fabsf(rate) < 1.0f) {
    divider = TIM_DIV256;  // 80 MHz / 256 = 312.5 kHz
    clk = 312500UL;
  } else {
    divider = TIM_DIV16;  // 80 MHz / 16 = 5 MHz
    clk = 5000000UL;
  }

  int32_t mrate = (int32_t)lroundf(rate * 1000.0f);
  uint32_t absM = (uint32_t)((mrate < 0) ? -mrate : mrate);
  // period = clk / (absM / 1000) = clk * 1000 / absM, in 1/65536-tick units
  uint64_t pf = ((uint64_t)clk * 65536UL * 1000UL) / absM;
  uint32_t period = (uint32_t)(pf >> 16);
  uint32_t rem = (uint32_t)(pf & 0xFFFF);
  uint32_t pulseTicks = (clk + 499999UL) / 500000UL;  // ~2 us STEP high
  if (pulseTicks < 2) {
    pulseTicks = 2;
  }

  bool wasRunning = (_mrate != 0);
  bool dividerChanged = wasRunning && (_divider != divider);

  _mrate = mrate;
  _periodTicks = period;
  _periodRem = rem;
  _pulseTicks = pulseTicks;
  _clk = clk;
  _divider = divider;

  if (dividerChanged) {
    _fracAccum = 0;
    timer1_disable();
    timer1_enable(divider, TIM_EDGE, TIM_SINGLE);
    timer1_write(_phase ? _pulseTicks : _periodTicks);
  } else if (!wasRunning) {
    _fracAccum = 0;
    _phase = 0;
    timer1_enable(divider, TIM_EDGE, TIM_SINGLE);
    timer1_write(_periodTicks);
  }
  // Already running with the same divider: the ISR picks up the new
  // period at the next step boundary. One-step latency is acceptable.
}

void StepEngine::stop() {
  _mrate = 0;
}
