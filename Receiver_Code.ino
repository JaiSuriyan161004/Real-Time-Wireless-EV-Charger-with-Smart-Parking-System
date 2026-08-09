/* Receiver_robust.ino
   Based on your Receiver_Final_LCD_Blackhack.ino but tolerant to packet size
   and prints debug info for incoming packets. Logic otherwise unchanged.
*/

#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LCD_I2C.h>

// ---------------------------------------------------------------------------
//                       TELEGRAM ADDON START
// ---------------------------------------------------------------------------
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

const char* WIFI_SSID = "ENTER YOUR WIFI NAME (HOTSPOT)";       // <<< 
const char* WIFI_PASS = "ENTER YOUR WIFI PASSWORD";       // <<< 

String BOT_TOKEN = "ENTER YOUR BOT TOKEN";            // <<< 
String CHAT_ID   = "ENTER YOUR CHAT ID";            // <<< 

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

void sendTelegram(String msg) {
  bot.sendMessage(CHAT_ID, msg, "");
}
// ---------------------------------------------------------------------------
//                       TELEGRAM ADDON END
// ---------------------------------------------------------------------------

// ----- PIN CONFIG -----
const int relayPin = 16;
const int irPin    = 15;
const int trigPin  = 5;
const int echoPin  = 18;

const int irPin2   = 4;
const int trigPin2 = 19;
const int echoPin2 = 13;

// ----- I2C LCD -----
LCD_I2C lcd(0x27, 16, 2);
const unsigned long DISPLAY_MS = 3000;

// ----- TRANSMITTER MAC -----
uint8_t transmitterMAC[] = {0x00,0x00,0x00,0x00,0x00,0x00};

// ----- PACKET STRUCT -----
typedef struct {
  uint8_t type;
  float voltage;
  float current;
  uint8_t status;
  uint8_t reason;
} Packet;

bool inputEnabled = true;
bool relayState = false;
bool reqSentForThisOnEvent = false;
volatile bool gotPacketFlag = false;
Packet lastPacket;
String lastAsciiPacket = "";

const unsigned long STATUS_WAIT_TIMEOUT_MS = 2000;

String macToString(const uint8_t *mac) {
  char buf[20];
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

long readUltrasonicDistance() {
  long duration;
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

long readUltrasonicDistance2() {
  long duration;
  digitalWrite(trigPin2, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin2, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin2, LOW);
  duration = pulseIn(echoPin2, HIGH, 30000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  Serial.print("[RX CB] From: "); Serial.print(macToString(info->src_addr));
  Serial.print("  len="); Serial.println(len);

  if (!relayState) {
    Serial.println("[RX CB] Ignored because relayState == OFF");
    return;
  }

  if (len >= (int)sizeof(Packet)) {
    memcpy(&lastPacket, data, sizeof(Packet));
    gotPacketFlag = true;
    lastAsciiPacket = "";
    return;
  }

  lastAsciiPacket = "";
  for (int i = 0; i < len; ++i) lastAsciiPacket += (char)data[i];
  lastAsciiPacket.trim();
  gotPacketFlag = true;
}


// ---------------------------------------------------------------------------
//                     *** UPDATED REQ SENDING FUNCTION ***
// ---------------------------------------------------------------------------
void sendREQ() {
  Serial.print("[RX] Sending REQ to: ");
  Serial.println(macToString(transmitterMAC));

  const char req[] = "REQ";
  esp_err_t r = esp_now_send(transmitterMAC, (const uint8_t*)req, sizeof(req)-1);

  if (r == ESP_OK) {
    Serial.println("[RX] REQ sent successfully.");
    reqSentForThisOnEvent = true;
  } else {
    Serial.print("[RX] REQ SEND FAILED. Error code: ");
    Serial.println((int)r);
  }
}
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
//                    DISPLAY + CONTROL FUNCTIONS (unchanged)
// ---------------------------------------------------------------------------
void lcdShow(const char *l1, const char *l2 = "") {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);
}

void forceOffWithTimeout(int seconds) {
  inputEnabled = false;
  relayState = false;
  digitalWrite(relayPin, HIGH);

  sendTelegram("⚠️ ERROR: Parking or charging failure detected.\nRelay OFF for " + String(seconds) + " seconds.");

  for (int t = seconds; t >= 1; --t) {
    lcdShow("ERROR!!", ("Wait " + String(t) + "s").c_str());
    delay(1000);
  }

  inputEnabled = true;
  lcdShow("WAITING","");
}

int processReceivedPacket() {
  if (!gotPacketFlag) return -2;
  gotPacketFlag = false;

  if (lastAsciiPacket.length() > 0) {
    String s = lastAsciiPacket;
    lastAsciiPacket = "";
    s.toUpperCase();
    if (s == "OK")   return 1;
    if (s == "FAIL") return 0;
    return -1;
  }

  if (lastPacket.type == 1) {
    return 2;
  } else if (lastPacket.type == 2) {
    return (lastPacket.status == 0) ? 1 : 0;
  }
  return -1;
}

// ---------------------------------------------------------------------------
//                        SETUP()
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH);
  pinMode(irPin, INPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(irPin2, INPUT);
  pinMode(trigPin2, OUTPUT);
  pinMode(echoPin2, INPUT);

  Wire.begin(21, 22);
  lcd.begin();
  lcd.backlight();
  lcdShow("WAITING","");

  // WiFi for Telegram
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // print connected channel (receiver)
Serial.print("WiFi connected. IP: ");
Serial.println(WiFi.localIP());
Serial.print("WiFi channel: ");
Serial.println(WiFi.channel());    // prints the current WiFi channel (e.g. 1..13 on 2.4GHz)

  Serial.println("\nWiFi connected.");
  secured_client.setInsecure();
  sendTelegram("📡 Receiver ESP32 Online.\nReady for parking + charging monitoring.");

  // ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[RX] ESP-NOW init failed!");
    lcdShow("ERROR","NOW FAIL");
    return;
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, transmitterMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  esp_err_t p = esp_now_add_peer(&peer);
  Serial.print("[RX] esp_now_add_peer(): ");
  Serial.println((int)p);
  Serial.print("[RX] transmitterMAC = ");
  Serial.println(macToString(transmitterMAC));
}

// ---------------------------------------------------------------------------
//                        LOOP()
// ---------------------------------------------------------------------------
void loop() {

  int irState = digitalRead(irPin);
  int irState2 = digitalRead(irPin2);
  long distance = readUltrasonicDistance();
  long distance2 = readUltrasonicDistance2();

  bool sensorTriggered = (
    (irState == LOW) &&
    (irState2 == LOW) &&
    (distance < 20) &&
    (distance2 < 20)
  );

  if (!relayState) {
  lcdShow("WAITING", "");
  delay(DISPLAY_MS);
}

  if (!inputEnabled) return;

  if (sensorTriggered && !relayState) {

    relayState = true;
    digitalWrite(relayPin, LOW);
    sendTelegram("🚗 Vehicle detected.\nCharging process started.");

    lcdShow("CHARGING","(waiting)");
    delay(DISPLAY_MS);

    sendREQ();

    unsigned long start = millis();
    bool statusHandled = false;

    while (millis() - start < STATUS_WAIT_TIMEOUT_MS) {

      int res = processReceivedPacket();

      if (res == 1) {
        lcdShow("CHARGING","OK");
        sendTelegram("⚡ Charging Allowed (OK).");
        statusHandled = true;
        break;
      }
      else if (res == 0) {
        lcdShow("ERROR!!","FAIL");
        sendTelegram("❌ Charging FAIL.\nVehicle misaligned or unsafe.");
        forceOffWithTimeout(10);
        return;
      }
      else if (res == -1) {
        sendTelegram("⚠️ Unexpected data received.\nShutting relay.");
        forceOffWithTimeout(10);
        return;
      }
    }

    if (!statusHandled) {
      sendTelegram("⚠️ No STATUS received.\nPossible false trigger.");
      forceOffWithTimeout(10);
    }

    return;
  }

  if (!sensorTriggered && relayState) {
    relayState = false;
    digitalWrite(relayPin, HIGH);
    sendTelegram("🔌 Charging Stopped.\nVehicle removed.");
    lcdShow("WAITING","");
    delay(DISPLAY_MS);
    return;
  }

  delay(200);
}
