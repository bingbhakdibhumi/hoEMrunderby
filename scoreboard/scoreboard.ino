#include <stdio.h>
#include <Arduino.h>

static byte bases = 0;   // 8-bit base state
static unsigned char score = 0;
static unsigned char outs = 0;

enum class GameState {
  IDLE,
  HIT,
  PITCH,
  GAME_OVER
};

static char hit_type[5] = "SDTH";

static GameState state = GameState::IDLE;

void setup() {
  Serial.begin(115200);
}

void loop() {
  if ((Serial.available() > 0)) {
    char cmd = Serial.read();

    // r = reset 
    if ((cmd == 'r') && (state == GameState::GAME_OVER)) {
      state = GameState::IDLE;
      Serial.println("Restarting game...");
    }

    if (outs >= 3) {
      state = GameState::GAME_OVER;
      outs = 0;
      Serial.println("GAME OVER!");
    }

    // p = pitch; s,d,t,h = 1B,2B,3B,HR; o = out
    if ((cmd == 'p') && (state == GameState::IDLE)) {
      state = GameState::PITCH;
      Serial.println("Pitch released, waiting for hit.");
    }
    else if ((cmd == 's') && (state == GameState::PITCH)) {
      hit(0);
    }
    else if ((cmd == 'd') && (state == GameState::PITCH)) {
      hit(1);
    }
    else if ((cmd == 't') && (state == GameState::PITCH)) {
      hit(2);
    }
    else if ((cmd == 'h') && (state == GameState::PITCH)) {
      hit(3);
    }
    else if ((cmd == 'o') && (state == GameState::PITCH)) {
      outs += 1;
      state = GameState::IDLE;
      Serial.println("Out!");
      Serial.print("Outs: ");
      Serial.println(outs);
      Serial.println();
    }
  }
}

// type: 0=single, 1=double, 2=triple, 3=HR
void hit(int type) {
  state = GameState::HIT;
  Serial.println("Hit detected!");

  byte scoreMask = 0b1111000;

  bases <<= (type + 1);
  bases += (0b1 << type);

  // Step 3: count runs (bits that overflow past 8 bits)
  byte overflow = bases & scoreMask;

  int runs = countBits(overflow);
  score += runs;

  // get rid of overflow
  bases &= 0x7;

  // Debug print
  Serial.print("Type: ");
  Serial.println(hit_type[type]);
  Serial.print("Runs scored: ");
  Serial.println(runs);
  Serial.print("Total score: ");
  Serial.println(score);
  Serial.print("Outs: ");
  Serial.println(outs);
  Serial.print("Bases: ");
  Serial.println(bases, BIN);
  Serial.println();

  state = GameState::IDLE;
}

// Count number of 1s in a byte
int countBits(byte x) {
  int count = 0;
  while (x) {
    count += x & 1;
    x >>= 1;
  }
  return count;
}

