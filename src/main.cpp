#include <Arduino.h>
#include "servo_control.h"

static const int SERVO_PIN = D6;
static const int BEAM_PIN  = D2;

enum class GameState {
  IDLE,
  WAITING_FOR_RESULT,
  RESETTING
};

static GameState state = GameState::IDLE;

// transition detect for breakbeam
static bool prevBeamBlocked = false;

// result timeout after swing starts
static unsigned long resultStartTime = 0;
static const unsigned long RESULT_TIMEOUT_MS =10000;

bool beamIsBlocked() {
  return digitalRead(BEAM_PIN) == LOW;
}

void enterState(GameState newState) {
  state = newState;

  if (state == GameState::IDLE) {
    Serial.println("State: IDLE");
    Serial.println("Press s to swing");
  }
  else if (state == GameState::WAITING_FOR_RESULT) {
    resultStartTime = millis();
    Serial.println("State: WAITING_FOR_RESULT");
  }
  else if (state == GameState::RESETTING) {
    Serial.println("State: RESETTING");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  servoSetup(SERVO_PIN);
  servoSetAngle(0.0f);

  pinMode(BEAM_PIN, INPUT_PULLUP);
  prevBeamBlocked = beamIsBlocked();

  enterState(GameState::IDLE);
}

void loop() {
  // 1. Read serial command
  if (Serial.available() > 0) {
    char cmd = Serial.read();

    if ((cmd == 's' || cmd == 'S') && state == GameState::IDLE) {
      Serial.println("Swing command received");
      servoMoveTo(270.0f, 5.0f);
      prevBeamBlocked = beamIsBlocked();   // clear stale edge
      enterState(GameState::WAITING_FOR_RESULT);
    }
  }

  // 2. State machine
  if (state == GameState::WAITING_FOR_RESULT) {
    bool beamBlocked = beamIsBlocked();

    // detect new beam block event
    if (beamBlocked && !prevBeamBlocked) {
      Serial.println("HIT: breakbeam triggered");
      servoMoveTo(0.0f, 5.0f);
      enterState(GameState::RESETTING);
    }
    else if (millis() - resultStartTime >= RESULT_TIMEOUT_MS) {
      Serial.println("MISS: timeout, no breakbeam");
      servoMoveTo(0.0f, 5.0f);
      enterState(GameState::RESETTING);
    }

    prevBeamBlocked = beamBlocked;
  }
  else if (state == GameState::RESETTING) {
    if (servoAtTarget()) {
      enterState(GameState::IDLE);
    }
  }

  // 3. Keep servo alive
  servoUpdate();
}