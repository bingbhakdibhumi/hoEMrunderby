#include <Arduino.h>
#include "servo_control.h"
#include <esp_now.h>
#include <WiFi.h>

/*** IMPORTANT!!
If Adafruit_MCP23X17 conflicts with ESP32 pin macros in your setup,
keep the header fix your teammate found.
***/
#include <Wire.h>
#include <Adafruit_MCP23X17.h>

// =====================================================
// ESP32 PINS
// =====================================================

static const int SERVO_PIN = D6;

// I2C pins for Nano ESP32
static const int I2C_SDA_PIN = A4;
static const int I2C_SCL_PIN = A5;

// =====================================================
// MCP23017 PIN MAP
// Adafruit library numbering:
// A0..A7 = 0..7
// B0..B7 = 8..15
// =====================================================

// Port A: pocket sensors
#define SINGLE1_PIN  0   // A0 yellow
#define SINGLE2_PIN  1   // A1 green
#define DOUBLE1_PIN  2   // A2 brown
#define DOUBLE2_PIN  3   // A3 purple
#define TRIPLE1_PIN  4   // A4 orange
#define TRIPLE2_PIN  5   // A5 white
#define HOMER_PIN    6   // A6 blue
#define OUT_PIN      7   // A7 red

// Port B: base LEDs
#define FIRST_BASE_LED   8   // B0
#define SECOND_BASE_LED  9   // B1
#define THIRD_BASE_LED   10  // B2

Adafruit_MCP23X17 mcp;

// =====================================================
// GAME STATE
// =====================================================

enum class GameState {
  IDLE,
  PITCHING,
  READY_TO_SWING,
  WAITING_FOR_RESULT,
  OUT,
  RESETTING,
  GAME_OVER,
  ERROR_STATE
};

static GameState state = GameState::IDLE;

// =====================================================
// BASEBALL STATE
// =====================================================

static byte bases = 0;   // bit0=1st, bit1=2nd, bit2=3rd
static unsigned char runs = 0;
static unsigned char outs = 0;
static char hit_type[5] = "SDTH";

// =====================================================
// ESPNOW / SWING INPUT
// =====================================================

typedef struct struct_message {
  int velo;
  int swing;
} struct_message;

struct_message myData;

static volatile bool swingSignal = false;
static volatile int velo = 0;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));

  Serial.print("Velocity: ");
  Serial.println(myData.velo);
  Serial.print("Swing: ");
  Serial.println(myData.swing);
  Serial.println();

  velo = myData.velo;
  swingSignal = (myData.swing != 0);
}

// =====================================================
// SENSOR EDGE TRACKING
// =====================================================

static bool prevSingle1Blocked = false;
static bool prevSingle2Blocked = false;

static bool prevDouble1Blocked = false;
static bool prevDouble2Blocked = false;

static bool prevTriple1Blocked = false;
static bool prevTriple2Blocked = false;

static bool prevHomerBlocked = false;
static bool prevOutBlocked = false;

// =====================================================
// TIMERS
// =====================================================

static unsigned long pitchStartTime = 0;
static const unsigned long PITCH_DELAY_MS = 2000;

static unsigned long readySwingStartTime = 0;
static const unsigned long SWING_TIMEOUT_MS = 3000;

static unsigned long resultStartTime = 0;
static const unsigned long RESULT_TIMEOUT_MS = 10000;

// =====================================================
// ROUND STATE
// =====================================================

static bool swingOccurred = false;

// =====================================================
// HELPERS
// =====================================================

int countBits(byte x) {
  int count = 0;
  while (x) {
    count += x & 1;
    x >>= 1;
  }
  return count;
}

void updateBaseLeds() {
  mcp.digitalWrite(FIRST_BASE_LED,  (bases & 0b001) ? HIGH : LOW);
  mcp.digitalWrite(SECOND_BASE_LED, (bases & 0b010) ? HIGH : LOW);
  mcp.digitalWrite(THIRD_BASE_LED,  (bases & 0b100) ? HIGH : LOW);
}

void printScoreboard() {
  Serial.print("Runs: ");
  Serial.println(runs);
  Serial.print("Outs: ");
  Serial.println(outs);
  Serial.print("Bases: ");
  Serial.println(bases, BIN);
  Serial.println();

  updateBaseLeds();
}

void resetScoreboard() {
  bases = 0;
  runs = 0;
  outs = 0;
  updateBaseLeds();
}

bool single1Blocked() { return mcp.digitalRead(SINGLE1_PIN) == LOW; }
bool single2Blocked() { return mcp.digitalRead(SINGLE2_PIN) == LOW; }

bool double1Blocked() { return mcp.digitalRead(DOUBLE1_PIN) == LOW; }
bool double2Blocked() { return mcp.digitalRead(DOUBLE2_PIN) == LOW; }

bool triple1Blocked() { return mcp.digitalRead(TRIPLE1_PIN) == LOW; }
bool triple2Blocked() { return mcp.digitalRead(TRIPLE2_PIN) == LOW; }

bool homerBlocked()   { return mcp.digitalRead(HOMER_PIN) == LOW; }
bool outBlocked()     { return mcp.digitalRead(OUT_PIN) == LOW; }

// =====================================================
// AUDIO PLACEHOLDERS
// =====================================================

void playAudioSingle()   { Serial.println("[AUDIO] Single"); }
void playAudioDouble()   { Serial.println("[AUDIO] Double"); }
void playAudioTriple()   { Serial.println("[AUDIO] Triple"); }
void playAudioHomeRun()  { Serial.println("[AUDIO] Home Run"); }
void playAudioOut()      { Serial.println("[AUDIO] Out"); }

// =====================================================
// STATE ENTRY
// =====================================================

void enterState(GameState newState) {
  state = newState;

  if (state == GameState::GAME_OVER) {
    Serial.println("State: GAME_OVER");
    Serial.println("GAME OVER! Press 'r' to restart.");
  }
  else if (state == GameState::IDLE) {
    swingOccurred = false;
    Serial.println("State: IDLE");
    Serial.println("Press p to pitch");
  }
  else if (state == GameState::PITCHING) {
    pitchStartTime = millis();
    swingOccurred = false;
    Serial.println("State: PITCHING");
    Serial.println("Pitch released, waiting before swing...");
  }
  else if (state == GameState::READY_TO_SWING) {
    readySwingStartTime = millis();
    Serial.println("BALL HAS BEEN PITCHED");
    Serial.println("State: READY_TO_SWING");
    Serial.println("Waiting for wireless swing...");
  }
  else if (state == GameState::WAITING_FOR_RESULT) {
    resultStartTime = millis();

    // initialize previous states so only NEW edges count
    prevSingle1Blocked = single1Blocked();
    prevSingle2Blocked = single2Blocked();

    prevDouble1Blocked = double1Blocked();
    prevDouble2Blocked = double2Blocked();

    prevTriple1Blocked = triple1Blocked();
    prevTriple2Blocked = triple2Blocked();

    prevHomerBlocked = homerBlocked();
    prevOutBlocked = outBlocked();

    Serial.println("State: WAITING_FOR_RESULT");
  }
  else if (state == GameState::OUT) {
    Serial.println("State: OUT");
    Serial.println("Ball fell into out pocket.");
    outs += 1;
    printScoreboard();

    if (outs >= 3) {
      enterState(GameState::GAME_OVER);
    } else {
      if (swingOccurred) {
        servoMoveTo(0.0f, 25.0f);
        enterState(GameState::RESETTING);
      } else {
        enterState(GameState::IDLE);
      }
    }
  }
  else if (state == GameState::RESETTING) {
    Serial.println("State: RESETTING");
  }
  else if (state == GameState::ERROR_STATE) {
    Serial.println("State: ERROR");
    Serial.println("No score pocket or out pocket detected before timeout.");
    Serial.println("Press p to try again.");
  }
}

// =====================================================
// BASEBALL SCORING LOGIC
// type: 0 single, 1 double, 2 triple, 3 home run
// =====================================================

void hit(int type) {
  Serial.println("Hit detected!");

  byte scoreMask = 0b1111000;

  bases <<= (type + 1);
  bases += (0b1 << type);

  byte overflow = bases & scoreMask;
  runs += countBits(overflow);
  bases &= 0x7;

  Serial.print("Type: ");
  Serial.println(hit_type[type]);
  Serial.print("Runs scored total: ");
  Serial.println(runs);
  Serial.print("Outs: ");
  Serial.println(outs);
  Serial.print("Bases: ");
  Serial.println(bases, BIN);
  Serial.println();

  updateBaseLeds();
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(2000);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  servoSetup(SERVO_PIN);
  servoSetAngle(0.0f);

  // I2C on Nano ESP32
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  if (!mcp.begin_I2C()) {
    Serial.println("Error initializing MCP23017");
    while (1) { delay(10); }
  }

  // Pocket sensors on Port A as inputs with pullups
  mcp.pinMode(SINGLE1_PIN, INPUT_PULLUP);
  mcp.pinMode(SINGLE2_PIN, INPUT_PULLUP);

  mcp.pinMode(DOUBLE1_PIN, INPUT_PULLUP);
  mcp.pinMode(DOUBLE2_PIN, INPUT_PULLUP);

  mcp.pinMode(TRIPLE1_PIN, INPUT_PULLUP);
  mcp.pinMode(TRIPLE2_PIN, INPUT_PULLUP);

  mcp.pinMode(HOMER_PIN, INPUT_PULLUP);
  mcp.pinMode(OUT_PIN, INPUT_PULLUP);

  // Base LEDs on Port B as outputs
  mcp.pinMode(FIRST_BASE_LED, OUTPUT);
  mcp.pinMode(SECOND_BASE_LED, OUTPUT);
  mcp.pinMode(THIRD_BASE_LED, OUTPUT);

  mcp.digitalWrite(FIRST_BASE_LED, LOW);
  mcp.digitalWrite(SECOND_BASE_LED, LOW);
  mcp.digitalWrite(THIRD_BASE_LED, LOW);

  prevSingle1Blocked = single1Blocked();
  prevSingle2Blocked = single2Blocked();

  prevDouble1Blocked = double1Blocked();
  prevDouble2Blocked = double2Blocked();

  prevTriple1Blocked = triple1Blocked();
  prevTriple2Blocked = triple2Blocked();

  prevHomerBlocked = homerBlocked();
  prevOutBlocked = outBlocked();

  Serial.println("Game starting...");
  printScoreboard();
  enterState(GameState::IDLE);
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  // serial commands:
  // p = pitch
  // r = reset
  // w = manual swing
  // while waiting for result:
  // s,d,t,h,o = manual result test

  if (Serial.available() > 0) {
    char cmd = Serial.read();

    if ((cmd == 'p' || cmd == 'P') &&
        (state == GameState::IDLE || state == GameState::ERROR_STATE)) {
      Serial.println("Pitch command received");
      enterState(GameState::PITCHING);
    }
    else if ((cmd == 'w' || cmd == 'W') && state == GameState::READY_TO_SWING) {
      Serial.println("Manual swing received");
      swingOccurred = true;
      servoMoveTo(270.0f, 25.0f);
      enterState(GameState::WAITING_FOR_RESULT);
    }
    else if (cmd == 'r' || cmd == 'R') {
      resetScoreboard();
      enterState(GameState::IDLE);
      Serial.println("Game reset.");
    }
    else if (state == GameState::WAITING_FOR_RESULT) {
      if (cmd == 's') {
        playAudioSingle();
        hit(0);
        if (swingOccurred) servoMoveTo(0.0f, 25.0f);
        enterState(GameState::RESETTING);
      }
      else if (cmd == 'd') {
        playAudioDouble();
        hit(1);
        if (swingOccurred) servoMoveTo(0.0f, 25.0f);
        enterState(GameState::RESETTING);
      }
      else if (cmd == 't') {
        playAudioTriple();
        hit(2);
        if (swingOccurred) servoMoveTo(0.0f, 25.0f);
        enterState(GameState::RESETTING);
      }
      else if (cmd == 'h') {
        playAudioHomeRun();
        hit(3);
        if (swingOccurred) servoMoveTo(0.0f, 25.0f);
        enterState(GameState::RESETTING);
      }
      else if (cmd == 'o') {
        playAudioOut();
        enterState(GameState::OUT);
      }
    }
  }

  // Wireless swing
  if (swingSignal && state == GameState::READY_TO_SWING) {
    Serial.println("Wireless swing received");
    swingOccurred = true;
    swingSignal = false;
    servoMoveTo(270.0f, 25.0f);
    enterState(GameState::WAITING_FOR_RESULT);
  }

  // State machine
  if (state == GameState::PITCHING) {
    if (millis() - pitchStartTime >= PITCH_DELAY_MS) {
      enterState(GameState::READY_TO_SWING);
    }
  }
  else if (state == GameState::READY_TO_SWING) {
    if (millis() - readySwingStartTime >= SWING_TIMEOUT_MS) {
      Serial.println("No swing entered in time. Watching for ball result...");
      enterState(GameState::WAITING_FOR_RESULT);
    }
  }
  else if (state == GameState::WAITING_FOR_RESULT) {
    bool single1Now = single1Blocked();
    bool single2Now = single2Blocked();

    bool double1Now = double1Blocked();
    bool double2Now = double2Blocked();

    bool triple1Now = triple1Blocked();
    bool triple2Now = triple2Blocked();

    bool homerNow = homerBlocked();
    bool outNow = outBlocked();

    if ((single1Now && !prevSingle1Blocked) ||
        (single2Now && !prevSingle2Blocked)) {
      playAudioSingle();
      hit(0);
      if (swingOccurred) servoMoveTo(0.0f, 25.0f);
      enterState(GameState::RESETTING);
    }
    else if ((double1Now && !prevDouble1Blocked) ||
             (double2Now && !prevDouble2Blocked)) {
      playAudioDouble();
      hit(1);
      if (swingOccurred) servoMoveTo(0.0f, 25.0f);
      enterState(GameState::RESETTING);
    }
    else if ((triple1Now && !prevTriple1Blocked) ||
             (triple2Now && !prevTriple2Blocked)) {
      playAudioTriple();
      hit(2);
      if (swingOccurred) servoMoveTo(0.0f, 25.0f);
      enterState(GameState::RESETTING);
    }
    else if (homerNow && !prevHomerBlocked) {
      playAudioHomeRun();
      hit(3);
      if (swingOccurred) servoMoveTo(0.0f, 25.0f);
      enterState(GameState::RESETTING);
    }
    else if (outNow && !prevOutBlocked) {
      playAudioOut();
      enterState(GameState::OUT);
    }
    else if (millis() - resultStartTime >= RESULT_TIMEOUT_MS) {
      if (swingOccurred) {
        servoMoveTo(0.0f, 25.0f);
      }
      enterState(GameState::ERROR_STATE);
    }

    prevSingle1Blocked = single1Now;
    prevSingle2Blocked = single2Now;

    prevDouble1Blocked = double1Now;
    prevDouble2Blocked = double2Now;

    prevTriple1Blocked = triple1Now;
    prevTriple2Blocked = triple2Now;

    prevHomerBlocked = homerNow;
    prevOutBlocked = outNow;
  }
  else if (state == GameState::RESETTING) {
    if (servoAtTarget()) {
      enterState(GameState::IDLE);
    }
  }

  servoUpdate();
}