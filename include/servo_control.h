#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include <Arduino.h>

void servoSetup(int pin);
void servoUpdate();

void servoSetAngle(float angleDeg);
void servoMoveTo(float targetAngleDeg, float stepDeg);

float servoGetAngle();
bool servoAtTarget();

#endif