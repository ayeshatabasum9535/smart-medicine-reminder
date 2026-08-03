/* Full integrated sketch:
   - WiFi (hardcoded)
   - OLED (SSD1306) on I2C SDA=21, SCL=22
   - Web UI: / and /save, /testDFPlayer
   - NTP time (IST)
   - DFPlayer Mini on Serial1 (RX=27, TX=26)
   - Buzzer on GPIO 18
   - STOP button on GPIO 19 (INPUT_PULLUP)
   - LEDs on GPIO 4,5,15
   - PCA9685 servo driver via I2C, channels 13/14/15 for lids
   - Smooth servo movements (600..2400us pulse) over ~2000ms
*/

#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_PWMServoDriver.h>
#include <esp_now.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Pins (confirmed)
#define BUZZER_PIN 18
#define STOP_BUTTON 19
#define LED1_PIN 2
#define LED2_PIN 25
#define LED3_PIN 33


// DFPlayer Serial pins (HardwareSerial1)
#define DFPLAYER_RX_PIN 27 // connect to DFPlayer TX
#define DFPLAYER_TX_PIN 26 // connect to DFPlayer RX

// I2C for OLED and PCA9685: SDA=21, SCL=22 (default on many ESP32 boards)

// WiFi (hardcoded)
const char* ssid = "FTOS";
const char* password = "mottakozhi";

// OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Time (NTP)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000); // IST offset, update every 60s

// Web server
WebServer server(80);

// Preferences
Preferences prefs;

// DFPlayer
HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;
bool dfPlayerReady = false;

// PCA9685
Adafruit_PWMServoDriver pca = Adafruit_PWMServoDriver(); // default 0x40

// Servo configuration (safe range)
const int SERVO_MIN_US = 600;   // safe min pulse width
const int SERVO_MAX_US = 2400;  // safe max pulse width
const int SERVO_OPEN_ANGLE = 90;  // open target (degrees)
const int SERVO_CLOSED_ANGLE = 0; // closed

// PCA9685 channels for lids
const uint8_t SERVO_CHANS[3] = {13, 14, 15}; // med1, med2, med3

// Servo movement state (non-blocking)
float servoCurrentAngle[3] = {0, 0, 0};
float servoStartAngle[3] = {0, 0, 0};
float servoTargetAngle[3] = {0, 0, 0};
unsigned long servoMoveStart[3] = {0, 0, 0};
unsigned long servoMoveDuration = 2000; // 2000 ms = ~2 seconds
bool servoMoving[3] = {false, false, false};

// ---------------- Robotic Arm Servo Channels ----------------
#define WAIST_CH     4
#define SHOULDER_CH  5
#define ELBOW_CH     6
#define GRIPPER_CH   7

// Home Position (EDIT LATER)
int HOME_WAIST = 10;
int HOME_SHOULDER = 30;
int HOME_ELBOW = 30;
int HOME_GRIPPER = 10;  // open

// Pick Position (TEMP angles for now)
int PICK_WAIST = 135;
int PICK_SHOULDER = 100;
int PICK_ELBOW = 40;
int PICK_GRIPPER = 120;  // close slightly


// Medicine data
const int NUM_MEDICINES = 3;
struct MedSchedule {
  int hour;
  int minute;
  String name;
  bool alertedToday;
} meds[NUM_MEDICINES];

// Alarm state
volatile bool alarmActive = false;
int currentMedicine = -1;

// Alarm timing (non-blocking phases)
unsigned long alarmTimer = 0;
int alarmPhase = 0; // 0=buzzer phase (beep 1s), 1=announce phase (play), 2=pause before next cycle

// Forward declarations
void saveMedData();
void loadMedData();
void startAlarm(int index);
void handleAlarm();
void stopAlarm();
void servoBegin();
void servoSetAngleImmediate(uint8_t chan, float angle);
void servoStartMove(int idx, float targetAngle);
void servoUpdate(); // call in loop()
int microsecondsToPwmTicks(int microseconds);

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  String msg = "";
  for (int i = 0; i < len; i++) msg += char(data[i]);

  Serial.print("ESP-NOW received: ");
  Serial.println(msg);

  if (msg == "ARM_START") {
    startArmSequence();
  }
}

void setup() {
  // Minimal Serial
  Serial.begin(115200);

  // Pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(STOP_BUTTON, INPUT_PULLUP);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
  Wire.begin(21, 22);
  delay(500);

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    // If OLED fails, stay alive but skip display usage
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 8);
  display.println("Medicine Reminder");
  display.display();

  // WiFi connect
  WiFi.begin(ssid, password);
  display.setCursor(0, 24);
  display.println("Connecting WiFi...");
  display.display();

  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    display.print(".");
    display.display();
    if (millis() - wifiStart > 60000) {
      // keep trying but continue looping (still blocks here until connect)
      wifiStart = millis();
    }
  }

  delay(100);
  Serial.print("ESP32 MAC Address (STA): ");
  Serial.println(WiFi.macAddress());
  Serial.print("ESP32 MAC Address (AP):  ");
  Serial.println(WiFi.softAPmacAddress());
  delay(100);

  // Show IP
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();

  timeClient.begin();


  // Reload I2C clean (WiFi corrupts first begin)
  Wire.begin(21, 22);
  delay(100);


  // Load meds from Preferences
  prefs.begin("meddata", false);
  loadMedData();
  prefs.end();

  // Initialize PCA9685
  
  pca.begin();
  pca.setPWMFreq(50); // servos ~50Hz
  delay(100);

  // Put servos to closed positions immediately
  for (int i = 0; i < 3; i++) {
    servoCurrentAngle[i] = SERVO_CLOSED_ANGLE;
    servoSetAngleImmediate(SERVO_CHANS[i], servoCurrentAngle[i]);
  }

  // Initialize DFPlayer
  dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  delay(200);
  if (dfPlayer.begin(dfSerial)) {
    dfPlayerReady = true;
    dfPlayer.volume(25);
    display.setCursor(0, 50);
    display.println("DFPlayer: OK");
  } else {
    dfPlayerReady = false;
    display.setCursor(0, 50);
    display.println("DFPlayer: FAIL");
  }
  display.display();

  // ✅ Sync ESP-NOW to WiFi channel
  // WiFi already connected above (to FTOS)
  WiFi.mode(WIFI_STA);  // must be STA for ESP-NOW

  // Make sure ESP-NOW uses the same channel as WiFi
  int ch = WiFi.channel();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW Init failed!");
  } else {
    esp_now_register_recv_cb(onDataRecv);
    Serial.printf("✅ ESP-NOW ready (using WiFi channel %d)\n", ch);
  }


  // Web server routes (inline HTML)
  server.on("/", HTTP_GET, []() {
    String html = "<!doctype html><html><head><meta charset='utf-8'><title>Medicine Reminder</title></head><body>";
    html += "<h2>Medicine Reminder Setup</h2>";
    html += "<form action='/save' method='POST'>";
    for (int i = 0; i < NUM_MEDICINES; i++) {
      html += "<b>Medicine " + String(i + 1) + "</b><br>";
      html += "Name: <input name='name" + String(i) + "' value='" + meds[i].name + "'><br>";
      html += "Hour (0-23): <input type='number' name='hour" + String(i) + "' min='0' max='23' value='" + String(meds[i].hour) + "'><br>";
      html += "Minute (0-59): <input type='number' name='min" + String(i) + "' min='0' max='59' value='" + String(meds[i].minute) + "'><br><br>";
    }
    html += "<input type='submit' value='Save'></form>";
    html += "<br><form action='/testDFPlayer' method='GET'><button type='submit'>Test DFPlayer</button></form>";
    html += "<br><p>Press STOP button (GPIO19) to stop alarm.</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, []() {
    for (int i = 0; i < NUM_MEDICINES; i++) {
      if (server.hasArg("name" + String(i))) meds[i].name = server.arg("name" + String(i));
      if (server.hasArg("hour" + String(i))) meds[i].hour = server.arg("hour" + String(i)).toInt();
      if (server.hasArg("min" + String(i))) meds[i].minute = server.arg("min" + String(i)).toInt();
    }
    prefs.begin("meddata", false);
    saveMedData();
    prefs.end();
    server.send(200, "text/html", "<h3>Saved! <a href='/'>Go back</a></h3>");
  });

  server.on("/testDFPlayer", HTTP_GET, []() {
    String result;
    if (dfPlayerReady) {
      dfPlayer.play(1);
      tone(BUZZER_PIN, 1200, 200);
      result = "<h3>DFPlayer test (track 1) started.</h3>";
    } else {
      result = "<h3>DFPlayer not detected.</h3>";
    }
    result += "<br><a href='/'>Back</a>";
    server.send(200, "text/html", result);
  });

  server.begin();

  delay(500);
  display.clearDisplay();
  display.display();
}

void loop() {
  server.handleClient();
  timeClient.update();

  int hour = timeClient.getHours();
  int minute = timeClient.getMinutes();

  // Update servo movement (non-blocking)
  servoUpdate();

  display.clearDisplay();

  if (alarmActive && currentMedicine != -1) {
    // Show active alarm message
    display.setCursor(0, 0);
    display.print("Time to take:");
    display.setCursor(0, 12);
    display.setTextSize(2);
    display.println(meds[currentMedicine].name);
    display.setTextSize(1);
    display.display();

    // Handle non-blocking alarm sequence (buzzer + dfplayer repeat)
    handleAlarm();
  } else {
    // Normal display
    display.setCursor(0, 0);
    display.print("Time: ");
    display.printf("%02d:%02d\n", hour, minute);

    for (int i = 0; i < NUM_MEDICINES; i++) {
      display.print(meds[i].name);
      display.print(": ");
      display.printf("%02d:%02d\n", meds[i].hour, meds[i].minute);
    }
    display.display();

    // Check schedule and start alarm if needed (prevent multiple triggers within same minute)
    for (int i = 0; i < NUM_MEDICINES; i++) {
      if (hour == meds[i].hour && minute == meds[i].minute && !meds[i].alertedToday) {
        meds[i].alertedToday = true;
        startAlarm(i);
        break;
      }
      if (!(hour == meds[i].hour && minute == meds[i].minute)) {
        meds[i].alertedToday = false;
      }
    }
  }

  delay(150); // small delay for display stability
}

// ------------------- Persistence -------------------
void saveMedData() {
  for (int i = 0; i < NUM_MEDICINES; i++) {
    prefs.putInt(("h" + String(i)).c_str(), meds[i].hour);
    prefs.putInt(("m" + String(i)).c_str(), meds[i].minute);
    prefs.putString(("n" + String(i)).c_str(), meds[i].name);
  }
}

void loadMedData() {
  for (int i = 0; i < NUM_MEDICINES; i++) {
    meds[i].hour = prefs.getInt(("h" + String(i)).c_str(), 9);
    meds[i].minute = prefs.getInt(("m" + String(i)).c_str(), 0);
    meds[i].name = prefs.getString(("n" + String(i)).c_str(), "Med" + String(i+1));
    meds[i].alertedToday = false;
  }
}

// ------------------- Alarm control -------------------
void startAlarm(int index) {
  alarmActive = true;
  currentMedicine = index;
  alarmPhase = 0;
  alarmTimer = millis();

  digitalWrite(LED1_PIN, index == 0 ? HIGH : LOW);
  digitalWrite(LED2_PIN, index == 1 ? HIGH : LOW);
  digitalWrite(LED3_PIN, index == 2 ? HIGH : LOW);

  Serial.print("Opening lid for med index: ");
  Serial.println(index);
  Serial.print("Target angle = ");
  Serial.println(SERVO_OPEN_ANGLE);


  // start servo open movement for this medicine
  servoStartMove(index, SERVO_OPEN_ANGLE);
}

void handleAlarm() {
  // Immediate stop if STOP pressed
  if (digitalRead(STOP_BUTTON) == LOW) {
    stopAlarm();
    return;
  }

  unsigned long now = millis();

  switch (alarmPhase) {
    case 0:
      // start buzzer phase (1s)
      tone(BUZZER_PIN, 1000);
      if (now - alarmTimer >= 1000) {
        noTone(BUZZER_PIN);
        alarmPhase = 1;
        alarmTimer = now;
      }
      break;

    case 1:
      // play DFPlayer track (1-based). Assume tracks are short <=2s
      if (dfPlayerReady) dfPlayer.play(currentMedicine + 1);
      alarmPhase = 2;
      alarmTimer = now;
      break;

    case 2:
      // wait ~2s for playback; then repeat
      if (now - alarmTimer >= 2000) {
        alarmPhase = 0;
        alarmTimer = now;
      }
      break;

    default:
      alarmPhase = 0;
      alarmTimer = now;
      break;
  }
}

void stopAlarm() {
  if (dfPlayerReady) dfPlayer.stop();
  noTone(BUZZER_PIN);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);

  // close lid(s) smoothly for the current medicine
  if (currentMedicine >= 0 && currentMedicine < NUM_MEDICINES) {
    servoStartMove(currentMedicine, SERVO_CLOSED_ANGLE);
  } else {
    // ensure all closed if unknown
    for (int i = 0; i < NUM_MEDICINES; i++) servoStartMove(i, SERVO_CLOSED_ANGLE);
  }

  alarmActive = false;
  currentMedicine = -1;

  // show acknowledgment briefly
  display.clearDisplay();
  display.setCursor(0, 20);
  display.println("Alarm stopped!");
  display.display();
  delay(800);
}

// ------------------- Servo helpers -------------------
// Immediately write angle to channel (no smoothing)
void servoSetAngleImmediate(uint8_t chan, float angle) {
  // clamp
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  // map angle to pulse microseconds within SAFE range
  int pulse_us = (int)((float)SERVO_MIN_US + (angle / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US));
  int ticks = microsecondsToPwmTicks(pulse_us);
  pca.setPWM(chan, 0, ticks);
}

// Start a smooth move for servo index (0..2) to targetAngle over servoMoveDuration
void servoStartMove(int idx, float targetAngle) {
  if (idx < 0 || idx > 2) return;
  servoStartAngle[idx] = servoCurrentAngle[idx];
  servoTargetAngle[idx] = targetAngle;
  servoMoveStart[idx] = millis();
  servoMoving[idx] = true;
}

// Call regularly to update servo positions (non-blocking)
void servoUpdate() {
  unsigned long now = millis();
  for (int i = 0; i < 3; i++) {
    if (!servoMoving[i]) continue;
    unsigned long elapsed = now - servoMoveStart[i];
    if (elapsed >= servoMoveDuration) {
      // finish
      servoCurrentAngle[i] = servoTargetAngle[i];
      servoSetAngleImmediate(SERVO_CHANS[i], servoCurrentAngle[i]);
      servoMoving[i] = false;
    } else {
      float t = (float)elapsed / (float)servoMoveDuration;
      // simple linear interpolation (can replace with easing if desired)
      float angle = servoStartAngle[i] + t * (servoTargetAngle[i] - servoStartAngle[i]);
      servoCurrentAngle[i] = angle;
      servoSetAngleImmediate(SERVO_CHANS[i], angle);
    }
  }
}

// Convert microseconds to PCA9685 ticks (12-bit)
int microsecondsToPwmTicks(int microseconds) {
  // For 50Hz, period = 20000 us. tick = period / 4096 ≈ 4.8828125 us
  const float usPerTick = 20000.0f / 4096.0f;
  int ticks = (int)((float)microseconds / usPerTick);
  if (ticks < 0) ticks = 0;
  if (ticks > 4095) ticks = 4095;
  return ticks;
}

// Smooth servo motion (blocking, but simple and safe)
void moveJoint(uint8_t ch, int fromA, int toA, int duration = 800) {
  unsigned long start = millis();
  while (millis() - start < duration) {
    float t = float(millis() - start) / duration;
    float angle = fromA + t * (toA - fromA);
    servoSetAngleImmediate(ch, angle);
    delay(5);
  }
  servoSetAngleImmediate(ch, toA);
}


void startArmSequence() {
  tone(BUZZER_PIN,2000,120);
  delay(150);
  tone(BUZZER_PIN,1200,120);
  Serial.println("✅ Remote ARM trigger received! Executing pick sequence...");

  moveJoint(15, 0, 180, 500);
  delay(600);

  // ---- Go to cup position ----
  moveJoint(WAIST_CH, HOME_WAIST, PICK_WAIST);
  moveJoint(SHOULDER_CH, HOME_SHOULDER, PICK_SHOULDER);
  moveJoint(ELBOW_CH, HOME_ELBOW, PICK_ELBOW);

  // ---- Grab cup ----
  moveJoint(GRIPPER_CH, HOME_GRIPPER, PICK_GRIPPER, 500);
  delay(600); // hold cup

  // ---- Return home w/ cup ----
  moveJoint(ELBOW_CH, PICK_ELBOW, HOME_ELBOW);
  moveJoint(SHOULDER_CH, PICK_SHOULDER, HOME_SHOULDER);
  moveJoint(WAIST_CH, PICK_WAIST, HOME_WAIST);

  // ---- Release cup ----
  //moveJoint(GRIPPER_CH, PICK_GRIPPER, HOME_GRIPPER, 500);

  moveJoint(15, 180, 0, 500);
  delay(600);

  Serial.println("✅ Arm cycle complete — Waiting for user...");
}
