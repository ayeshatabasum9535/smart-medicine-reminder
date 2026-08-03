# HART – Smart Medicine Reminder 

HART (Healthcare Assistive Reminder Technology)

## What it actually does

- Keeps track of up to 3 medicines, each with its own name and scheduled time.
- Syncs real time over WiFi using NTP, so no manual clock setting.
- Shows the current time and upcoming medicine schedule on a small OLED screen.
- When it's time for a dose:
  - Lights up an LED for that specific medicine.
  - Sounds a buzzer.
  - Plays an audio announcement through a DFPlayer Mini (MP3 module).
  - Opens the correct compartment lid using a servo motor.
- A physical STOP button silences the alarm and closes the lid again.
- A simple web page (hosted directly on the ESP32) lets you set/edit medicine names and times from your phone or laptop — no app required.
- Includes an optional 4-axis robotic arm sequence that can pick up and deliver a cup, triggered wirelessly.
- A second ESP32 acts as a remote control — press a button on it and it sends a signal (over ESP-NOW, no WiFi router needed for this part) to kick off the arm sequence on the main unit.

## Hardware used

- 2x ESP32 dev boards (one as the main controller, one as the remote)
- SSD1306 OLED display (128x64, I2C)
- PCA9685 16-channel PWM/servo driver
- DFPlayer Mini MP3 module + speaker + micro SD card (for audio alerts)
- Servo motors — 3 for the medicine compartment lids, plus 4 more if you're adding the robotic arm (waist, shoulder, elbow, gripper)
- Buzzer
- 3x LEDs (one per medicine slot)
- Push button (for STOP, and another one on the remote)
- Standard jumper wires, breadboard/perfboard, and a 5V power supply beefy enough for the servos

### Pin reference (main unit)

| Component            | Pin(s)         |
|-----------------------|----------------|
| OLED (I2C)            | SDA 21, SCL 22 |
| DFPlayer Mini (Serial1)| RX 27, TX 26  |
| Buzzer                | GPIO 18        |
| STOP button            | GPIO 19 (INPUT_PULLUP) |
| LED 1 / 2 / 3          | GPIO 2 / 25 / 33 |
| PCA9685 servo channels | 13, 14, 15 (lids) |
| Robotic arm channels   | Waist 4, Shoulder 5, Elbow 6, Gripper 7 |

### Pin reference (remote)

| Component | Pin |
|-----------|-----|
| Button    | GPIO 12 (INPUT_PULLUP) |

## Libraries you'll need

Install these through the Arduino IDE Library Manager before compiling:

- `WiFi.h` / `esp_wifi.h` (bundled with the ESP32 board package)
- `WebServer.h` (bundled with the ESP32 board package)
- `Preferences.h` (bundled with the ESP32 board package)
- `Wire.h` (bundled)
- `Adafruit_SSD1306` (and its dependency `Adafruit_GFX`)
- `NTPClient`
- `WiFiUdp.h` (bundled)
- `DFRobotDFPlayerMini`
- `Adafruit_PWMServoDriver`
- `esp_now.h` (bundled with the ESP32 board package)

## Getting it running

1. **Flash the main unit** with `HART_v8.ino`.
   - Open the sketch in Arduino IDE (or PlatformIO).
   - Update the WiFi credentials near the top of the file:
     ```cpp
     const char* ssid = "YOUR_WIFI_NAME";
     const char* password = "YOUR_WIFI_PASSWORD";
     ```
   - Select your ESP32 board and port, then upload.
   - Open the Serial Monitor at 115200 baud — it'll print the board's MAC address and the local IP once WiFi connects. You'll need the MAC address for the remote.

2. **Set up the medicine schedule.**
   - Once connected, the OLED will show the device's IP address.
   - Visit that IP in a browser on the same network.
   - Fill in the medicine names and times, hit save. Settings are stored in flash (via `Preferences`), so they survive a reboot.

3. **Flash the remote** with `HART_remote_1.ino` (optional — only needed if you're using the robotic arm feature).
   - Update `receiverAddress[]` with the MAC address of your main unit (printed in step 1).
   - Make sure `ESPNOW_CHANNEL` matches the WiFi channel your main unit ends up on (the main sketch auto-syncs its ESP-NOW channel to whatever channel your router assigns).
   - Upload to the second ESP32.

4. **Load audio files onto the SD card** for the DFPlayer Mini. Track 1 corresponds to medicine 1, track 2 to medicine 2, and so on — name your MP3 files accordingly (usually `0001.mp3`, `0002.mp3`, etc., depending on how your DFPlayer module expects them).

5. Power everything up, and you're good to go. When a scheduled time hits, the buzzer, LED, audio, and lid servo for that medicine will all trigger automatically.

## Using it day to day

- The OLED shows the current time and each medicine's schedule when idle.
- When an alarm goes off, press the physical **STOP button** to acknowledge it — this stops the buzzer/audio and closes the lid again.
- Update schedules anytime from the web page — no need to reflash the device.
- There's also a `/testDFPlayer` route on the web server if you just want to test the speaker without waiting for a real alarm.
- If you've built the robotic arm add-on, pressing the remote button sends an `ARM_START` signal over ESP-NOW, which runs the pick-and-deliver sequence.

## Known limitations / things to keep in mind

- WiFi credentials are currently hardcoded in the sketch — fine for personal use, but swap in something like WiFiManager if you want a nicer setup flow.
- The web page has no authentication, so anyone on your network can change the schedule. Not a big deal at home, but worth knowing.
- Robotic arm angles (`HOME_*` / `PICK_*` variables) are rough starting values — you'll need to tune these for your specific arm build and cup placement.
- The WiFi connect loop in `setup()` will keep retrying indefinitely if it can't connect, so make sure your credentials are correct before deploying.

## Project structure


## Why "HART"?

Honestly, it started as just a project codename and stuck. Feel free to rename it in your own fork — the code doesn't care what you call it.

## Contributing

This is very much a hobby/home project, but if you spot a bug, have a wiring improvement, or want to add features (Telegram/WhatsApp alerts, multiple users, a proper mobile app, etc.), feel free to open an issue or a PR.



