#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

#define BUTTON_PIN 12  // your button pin

// --- Main ESP32’s MAC address (receiver) ---
uint8_t receiverAddress[] = {0x68, 0x25, 0xDD, 0x32, 0x0F, 0x88};

// --- Wi-Fi channel (must match main ESP32) ---
#define ESPNOW_CHANNEL 10

// ✅ Updated send callback for ESP-IDF v5.5 (Arduino Core 3.x)
void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("Delivery: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  delay(200);

  Serial.print("Remote ESP32 MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("Using Channel: %d\n", ESPNOW_CHANNEL);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed ❌");
    return;
  }

  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Peer add failed ❌");
    return;
  }

  Serial.println("Remote ready ✅");
}

void loop() {
  static bool last = HIGH;
  bool now = digitalRead(BUTTON_PIN);

  if (now == LOW && last == HIGH) {
    const char *msg = "ARM_START";
    esp_now_send(receiverAddress, (uint8_t*)msg, strlen(msg));
    Serial.println("Button pressed → ARM_START sent");
    delay(300);
  }

  last = now;
}

//Final
