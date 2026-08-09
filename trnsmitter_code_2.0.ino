#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"
#include <Wire.h>
#include <Adafruit_INA219.h>

// ---------------- INA219 SENSOR ----------------
Adafruit_INA219 ina219;

// ---------------- PACKET STRUCT ----------------
typedef struct {
  uint8_t type;
  float voltage;
  float current;
  uint8_t status;
  uint8_t reason;
} Packet;

Packet packetOUT;

// ---------------- GLOBALS ----------------
float measuredV = 0, measuredI = 0;

float thresholdV_min = 0, thresholdV_max = 0;
float thresholdI_min = 0, thresholdI_max = 0;   // (Not used anymore)

bool initialThresholdSet = false;

uint8_t receiverMAC[] = {0x00,0x00,0x00,0x00,0x00,0x00};

// ------------------------------------------------
// READ SENSOR
// ------------------------------------------------
void readSensor() {
  measuredV = ina219.getBusVoltage_V();
  measuredI = ina219.getCurrent_mA();
}

// ------------------------------------------------
// AUTO-CALIBRATE THRESHOLDS (FIRST STABLE READING)
// ------------------------------------------------
void autoCalibrateThresholds() {
  Serial.println("\n[TX] Auto-calibrating thresholds...");

  float sumV = 0, sumI = 0;
  const int samples = 10;

  for (int i = 0; i < samples; i++) {
    readSensor();
    sumV += measuredV;
    sumI += measuredI;

    Serial.print("[TX] Calibration Sample ");
    Serial.print(i + 1);
    Serial.print(": V=");
    Serial.print(measuredV);
    Serial.print(" I=");
    Serial.println(measuredI);

    delay(200);
  }

  float avgV = sumV / samples;

  // ------------------- VOLTAGE THRESHOLDS ONLY -------------------
  thresholdV_min = avgV * 0.90;
  thresholdV_max = avgV * 1.10;

  // ----------- CURRENT THRESHOLD REMOVED (DUMMY VALUES set) ------
  thresholdI_min = -999999;
  thresholdI_max =  999999;

  initialThresholdSet = true;

  Serial.println("\n------ AUTO THRESHOLDS SET ------");
  Serial.print("V_min = "); Serial.println(thresholdV_min);
  Serial.print("V_max = "); Serial.println(thresholdV_max);

  Serial.println("I_min = NOT USED");
  Serial.println("I_max = NOT USED");
  Serial.println("---------------------------------\n");
}

// ------------------------------------------------
// RECEIVE CALLBACK
// ------------------------------------------------
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {

  Serial.print("[TX] Received: ");
  for (int i = 0; i < len; i++) Serial.print((char)data[i]);
  Serial.println();

  if (len == 3 && strncmp((const char*)data, "REQ", 3) == 0) {

    Serial.println("[TX] REQ Received.");

    if (!initialThresholdSet) {
      Serial.println("[TX] ERROR: Thresholds NOT calibrated!");
      return;
    }

    readSensor();

    Serial.print("[TX] Live Values → V=");
    Serial.print(measuredV);
    Serial.print(" I=");
    Serial.println(measuredI);

    // ---------------- SEND TYPE 1 PACKET (VALUES) ----------------
    packetOUT.type = 1;
    packetOUT.voltage = measuredV;
    packetOUT.current = measuredI;
    packetOUT.status = 0;
    packetOUT.reason = 0;

    esp_now_send(info->src_addr, (uint8_t*)&packetOUT, sizeof(packetOUT));
    Serial.println("[TX] Sent TYPE=1 (VALUES)");

    // ---------------- SEND TYPE 2 PACKET (STATUS) ----------------
    packetOUT.type = 2;

    // *** CURRENT THRESHOLD REMOVED — ONLY VOLTAGE CHECK ***
    if (measuredV < thresholdV_min || measuredV > thresholdV_max) {

      packetOUT.status = 1;
      packetOUT.reason = 1;

      Serial.println("[TX] STATUS = FAIL (Voltage Out of Range)");

    } else {
      packetOUT.status = 0;
      packetOUT.reason = 0;
      Serial.println("[TX] STATUS = OK");
    }

    esp_now_send(info->src_addr, (uint8_t*)&packetOUT, sizeof(packetOUT));
    Serial.println("[TX] Sent TYPE=2 (STATUS)");
  }
}

// ------------------------------------------------
// SEND CALLBACK
// ------------------------------------------------
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("[TX] Delivery: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// ------------------------------------------------
// SETUP
// ------------------------------------------------
void setup() {

  Serial.begin(115200);
  delay(300);

  Serial.println("\n=== ESP32 TRANSMITTER ONLINE ===");

  if (!ina219.begin()) {
    Serial.println("[TX] INA219 not found! Check wiring.");
    while (1);
  }

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(11, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[TX] ESP-NOW init failed!");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[TX] Failed to add peer!");
  }

  autoCalibrateThresholds();

  Serial.println("Waiting for REQ from Receiver...");
}

// ------------------------------------------------
// LOOP
// ------------------------------------------------
void loop() {
  readSensor();
  delay(100);
}
