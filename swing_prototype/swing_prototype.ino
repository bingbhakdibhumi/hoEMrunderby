#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <Wire.h>
#include <esp_now.h>
#include <WiFi.h>

#define BNO08X_CS 10
#define BNO08X_INT 9
#define BNO08X_RESET -1

// Servo Controller MAC Address --> 48:CA:43:2F:69:44 
uint8_t broadcastAddress[] = {0x48, 0xCA, 0x43, 0x2F, 0x69, 0x44};

// // Test Controller MAC Address --> 48:CA:43:2E:11:80
// uint8_t broadcastAddress[] = {0x48, 0xCA, 0x43, 0x2E, 0x11, 0x80};

// Structure example to send data
// Must match the receiver structure
typedef struct struct_message {
    int velo;
    bool swing;
} struct_message;

// Create a struct_message called myData
struct_message myData;

esp_now_peer_info_t peerInfo;

// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

// Velocity tracking
float velocity = 0.0;
float lastTime = 0.0;
const float VELOCITY_THRESHOLD = 6.5;

// Add timeout and recovery variables
unsigned long lastSensorReadTime = 0;
const unsigned long SENSOR_TIMEOUT = 1000;  // 1 second timeout
unsigned long lastSerialOutput = 0;
const unsigned long SERIAL_OUTPUT_INTERVAL = 100;  // Print every 100ms max

void setReports(sh2_SensorId_t reportType, long report_interval) {
  Serial.println("Setting desired reports");
  if (!bno08x.enableReport(reportType, report_interval)) {
    Serial.println("Could not enable report");
  }
}

void setup(void) {
  Serial.begin(115200);
  delay(100);  // Give serial time to initialize
  
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for Send CB to
  // get the status of Trasnmitted packet
  esp_now_register_send_cb(esp_now_send_cb_t(OnDataSent));
  
  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  if (!bno08x.begin_I2C()) {
    Serial.println("Failed to find BNO085 chip");
    while (1) { 
      delay(10); 
    }
  }
  Serial.println("BNO08x Found!");

  // Enable linear acceleration report with slower interval to avoid overflow
  if (!bno08x.enableReport(SH2_LINEAR_ACCELERATION, 10000)) {  // 10ms = 100Hz
    Serial.println("Could not enable linear acceleration report");
  }
  
  lastTime = millis() / 1000.0;
  lastSensorReadTime = millis();
  lastSerialOutput = millis();

  Serial.println("Ready to detect swings");
}

void loop() {
  float currentTime = millis() / 1000.0;
  float deltaTime = currentTime - lastTime;
  lastTime = currentTime;
  
  // Attempt to read sensor with timeout protection
  if (bno08x.getSensorEvent(&sensorValue)) {
    lastSensorReadTime = millis();  // Reset timeout counter
    
    if (sensorValue.sensorId == SH2_LINEAR_ACCELERATION) {
      float accelX = sensorValue.un.linearAcceleration.x;
      float accelY = sensorValue.un.linearAcceleration.y;
      float accelZ = sensorValue.un.linearAcceleration.z;
      
      float accelMagnitude = sqrt(
        accelX * accelX +
        accelY * accelY +
        accelZ * accelZ
      );
      
      // Integrate acceleration to get velocity
      velocity += accelMagnitude * deltaTime;
      
      // Apply damping
      velocity *= 0.95;
      
      // Check if threshold exceeded
      if (velocity > VELOCITY_THRESHOLD) {
        detectSwing();
        velocity = 0;
      }
      
      // Print debug info at reduced rate to prevent buffer overflow
      if (millis() - lastSerialOutput >= SERIAL_OUTPUT_INTERVAL) {
        Serial.print("V:");
        Serial.print(velocity, 2);
        Serial.print(" A:");
        Serial.println(accelMagnitude, 2);
        lastSerialOutput = millis();
      }
    }
  } else {
    // Check for sensor timeout
    if (millis() - lastSensorReadTime > SENSOR_TIMEOUT) {
      Serial.println("ERROR: Sensor timeout! Attempting recovery...");
      recoverSensor();
      lastSensorReadTime = millis();
    }
  }
  
  delay(50);  // delay between sensor reads
}

void detectSwing() {
  myData.velo = 67;
  myData.swing = true;
  
  // Send message via ESP-NOW
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
   
  if (result == ESP_OK) {
    Serial.println("Sent with success");
  }
  else {
    Serial.println("Error sending the data");
  }
  Serial.println(">>> SWING DETECTED <<<");
  Serial.flush();  // Ensure message is sent before continuing
}

void recoverSensor() {
  Serial.println("Reinitializing BNO08x...");
  Serial.flush();
  
  // Attempt to reinitialize
  if (bno08x.begin_I2C()) {
    Serial.println("BNO08x reinitialized successfully");
    if (!bno08x.enableReport(SH2_LINEAR_ACCELERATION, 10000)) {
      Serial.println("Failed to re-enable report");
    }
    lastSensorReadTime = millis();
  } else {
    Serial.println("Failed to reinitialize BNO08x!");
  }
}

// void quaternionToEuler(float qr, float qi, float qj, float qk, euler_t* ypr, bool degrees = false) {

//     float sqr = sq(qr);
//     float sqi = sq(qi);
//     float sqj = sq(qj);
//     float sqk = sq(qk);

//     ypr->yaw = atan2(2.0 * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr));
//     ypr->pitch = asin(-2.0 * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr));
//     ypr->roll = atan2(2.0 * (qj * qk + qi * qr), (-sqi - sqj + sqk + sqr));

//     if (degrees) {
//       ypr->yaw *= RAD_TO_DEG;
//       ypr->pitch *= RAD_TO_DEG;
//       ypr->roll *= RAD_TO_DEG;
//     }
// }

// void quaternionToEulerRV(sh2_RotationVectorWAcc_t* rotational_vector, euler_t* ypr, bool degrees = false) {
//     quaternionToEuler(rotational_vector->real, rotational_vector->i, rotational_vector->j, rotational_vector->k, ypr, degrees);
// }

// void quaternionToEulerGI(sh2_GyroIntegratedRV_t* rotational_vector, euler_t* ypr, bool degrees = false) {
//     quaternionToEuler(rotational_vector->real, rotational_vector->i, rotational_vector->j, rotational_vector->k, ypr, degrees);
// }

// void loop() {

//   if (bno08x08x.wasReset()) {
//     Serial.print("sensor was reset ");
//     setReports(reportType, reportIntervalUs);
//   }
  
//   if (bno08x.getSensorEvent(&sensorValue)) {
//     // in this demo only one report type will be received depending on FAST_MODE define (above)
//     switch (sensorValue.sensorId) {
//       case SH2_ARVR_STABILIZED_RV:
//         quaternionToEulerRV(&sensorValue.un.arvrStabilizedRV, &ypr, true);
//       case SH2_GYRO_INTEGRATED_RV:
//         // faster (more noise?)
//         quaternionToEulerGI(&sensorValue.un.gyroIntegratedRV, &ypr, true);
//         break;
//     }
//     static long last = 0;
//     long now = micros();
//     Serial.print(now - last);             Serial.print("\t");
//     last = now;
//     Serial.print(sensorValue.status);     Serial.print("\t");  // This is accuracy in the range of 0 to 3
//     Serial.print(ypr.yaw);                Serial.print("\t");
//     Serial.print(ypr.pitch);              Serial.print("\t");
//     Serial.println(ypr.roll);
//   }

// }
