# CogniPet: Hospital-Induced Delirium Detection Device

## Hardware Setup

- **ESP32-S3** microcontroller
- **Grove LCD RGB Backlight** (16x2 character LCD)
  - I2C address: 0x3E (LCD), 0x62 (RGB backlight)
- **3 Buttons** (BTN1, BTN2, BTN3) on pins 12, 13, 14
- **LED** on pin 11 (for feedback)
- **I2C** on pins 1 (SDA), 2 (SCL)

## Required Libraries

### Built-in ESP32 Libraries (No Installation Needed)
- `Wire.h` - I2C communication
- `BLEDevice.h`, `BLEServer.h`, `BLEUtils.h`, `BLE2902.h` - Bluetooth Low Energy
- `Preferences.h` - Non-volatile storage

### Arduino IDE Setup

1. **Install ESP32 Board Support:**
   - Open Arduino IDE
   - Go to **File > Preferences**
   - In "Additional Board Manager URLs", add:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Click OK

2. **Install ESP32 Board Package:**
   - Go to **Tools > Board > Boards Manager**
   - Search for "ESP32"
   - Install "esp32 by Espressif Systems" (latest version)

3. **Select Board:**
   - Go to **Tools > Board > ESP32 Arduino**
   - Select **"ESP32S3 Dev Module"**

4. **Configure Board Settings:**
   - **Tools > Port:** Select your ESP32-S3 COM port
   - **Tools > USB CDC On Boot:** Enabled (for Serial output)
   - **Tools > USB DFU On Boot:** Disabled
   - **Tools > USB Firmware MSC On Boot:** Disabled
   - **Tools > PSRAM:** Enabled (if your board has PSRAM)
   - **Tools > Partition Scheme:** Default 4MB with spiffs
   - **Tools > Core Debug Level:** None (or Info for debugging)

5. **Upload:**
   - Click **Upload** button
   - Wait for compilation and upload to complete

## Features

### Cognitive Assessment (First Boot)
On first power-on, the device runs a cognitive assessment:

1. **Orientation Test** (3 questions)
   - Day of week
   - Time of day
   - Location awareness

2. **Memory Test** (Simon Says style)
   - Shows sequence of 3 buttons
   - Patient repeats the sequence

3. **Attention Test** (Reaction time)
   - Press button A when star appears
   - 5 trials, measures reaction time

4. **Executive Function Test** (Sequencing)
   - Order daily activities correctly

**Scoring:**
- 10-12 points: No impairment (Green)
- 7-9 points: Mild concern (Yellow)
- 4-6 points: Moderate impairment (Orange)
- 0-3 points: Severe impairment (Red)

### Virtual Pet Mode (After Assessment)

**Pet Interactions:**
- **Button A (BTN1):** Feed pet
- **Button B (BTN2):** Play mini-game (reaction test)
- **Button C (BTN3):** Clean pet

**Pet Status:**
- Happiness level (affects pet face: ^_^ / -_- / T_T)
- Hunger level (shows [HUN] when hungry)
- Cleanliness (shows [DRT] when dirty)

**RGB Backlight Colors:**
- **Green:** Pet is happy
- **Yellow:** Pet is neutral
- **Red:** Pet is sad/needs attention

**LED Feedback:**
- **3 quick flashes:** Correct answer (green pattern)
- **1 long flash:** Incorrect answer (red pattern)

## BLE Data Transmission

The device broadcasts assessment and interaction data via Bluetooth Low Energy:

**Service UUID:** `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

**Characteristics:**
- **Assessment Data:** `beb5483e-36e1-4688-b7f5-ea07361b26a8`
  - Sent after cognitive assessment completes
  - Contains: scores, response times, alert level
  
- **Interaction Data:** `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e`
  - Sent after each pet interaction
  - Contains: interaction type, response time, success status

**Device Name:** "CogniPet"

## Data Structures

### AssessmentResult (32 bytes)
```cpp
struct AssessmentResult {
  uint32_t timestamp;        // milliseconds since boot
  uint8_t orientation_score; // 0-3
  uint8_t memory_score;      // 0-3
  uint8_t attention_score;   // 0-3
  uint8_t executive_score;   // 0-3
  uint8_t total_score;       // 0-12
  uint16_t avg_response_time_ms;
  uint8_t alert_level;       // 0=green, 1=yellow, 2=orange, 3=red
};
```

### InteractionLog (16 bytes)
```cpp
struct InteractionLog {
  uint32_t timestamp;
  uint8_t interaction_type;  // 0=feed, 1=play, 2=clean, 3=game
  uint16_t response_time_ms;
  uint8_t success;           // 1=success, 0=fail
  int8_t mood_selected;      // -1 if not mood check
};
```

## Troubleshooting

### LCD Not Displaying
- Check I2C connections (SDA pin 1, SCL pin 2)
- Verify I2C addresses: 0x3E (LCD), 0x62 (RGB)
- Check power supply (5V for Grove LCD)

### Buttons Not Working
- Verify pull-up resistors (code uses INPUT_PULLUP)
- Check button connections to pins 12, 13, 14
- Buttons should connect to GND when pressed

### BLE Not Advertising
- Check Serial monitor for "BLE advertising started" message
- Ensure ESP32-S3 has Bluetooth enabled
- Try resetting the device

### Assessment Not Running
- First boot only: Clear preferences by holding button during boot
- Or re-upload sketch to reset

## Serial Monitor Output

Open Serial Monitor at 115200 baud to see:
- I2C scan results
- BLE connection status
- Assessment scores
- Interaction logs

## Integration with Backend

The backend server (in `sketch_nov2a/backend/`) can receive BLE data via:
1. ESP32 BLE gateway (separate ESP32 device)
2. Smartphone app bridge
3. Direct WiFi connection (future enhancement)

See backend API endpoints in `server.py` for receiving assessment and interaction data.

