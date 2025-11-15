# NightWatch: Hospital-Induced Delirium Detection & Prevention System

A comprehensive IoT healthcare monitoring system that combines **real-time sleep monitoring** and **cognitive assessment** to detect and prevent hospital-induced delirium in patients.

## 🎯 Overview

NightWatch is an integrated system with **two complementary components** that work together to detect delirium:

1. **Sleep Monitoring** (Arduino UNO R4): Tracks sleep patterns, movement, and environmental factors
2. **Cognitive Assessment** (ESP32-S3 CogniPet): Performs cognitive tests and tracks cognitive function over time (now supports real-world orientation prompts via Wi‑Fi/NTP)
3. **Patient Navigator (demo site)**: Switch between example patient profiles at `http://localhost:8000/patients` for demos without moving the physical hardware

**Together**, these components provide early warning signs of delirium by monitoring both:
- **Sleep disturbances** (disrupted sleep patterns are a key indicator)
- **Cognitive decline** (direct assessment of cognitive function)

## 🏥 How It Works

Hospital-induced delirium is often preceded by:
- **Sleep disruption** (frequent awakenings, restlessness)
- **Cognitive impairment** (confusion, disorientation, memory issues)

NightWatch monitors both indicators:
- **Sleep sensors** detect movement patterns and sleep quality
- **Cognitive device** performs regular assessments to track cognitive function
- **Combined data** provides comprehensive delirium risk assessment

---

## 📦 Project Structure

```
sketch_nov2a/
├── backend/                    # Python backend (shared by both components)
│   ├── server.py              # FastAPI server
│   ├── database.py            # Database models (sleep + cognitive data)
│   ├── pipeline.py            # Sleep data processing
│   ├── ble_bridge.py          # BLE bridge for CogniPet
│   └── requirements.txt
├── cognipet_esp32/            # CogniPet ESP32 firmware
│   ├── cognipet_esp32.ino     # Cognitive assessment device
│   └── README.md
├── sketch_nov2a.ino           # NightWatch Arduino UNO R4 sketch (sleep monitoring)
├── restart_all.sh             # Restart all services
├── stop_all.sh                # Stop all services
└── status.sh                  # Check system status
```

---

## 🌙 Component 1: Sleep Monitoring (Arduino UNO R4)

Monitors sleep patterns, movement, and environmental factors that may indicate delirium risk.

### Features

- **Real-time monitoring** via WebSocket streaming
- **Intelligent filtering** with adaptive thresholds and EMA smoothing
- **3-level activity classification** (Idle / Slight movement / Needs attention)
- **Environmental monitoring** (sound, light, temperature)
- **Dual interface**: Clinician-friendly overview + detailed technical dashboard
- **Data persistence** with SQLite database
- **Advanced analytics** with Plotly visualizations

### Hardware Requirements

- Arduino UNO R4 Minima
- 3× Piezo sensors (Head: A3, Body: A4, Leg: A5)
- Sound sensor (A0)
- Light sensor (A1)
- Temperature sensor (A2)
- Push button (D2 to GND)

### Installation

#### 1. Arduino Setup

Upload `sketch_nov2a.ino` to your Arduino UNO R4 Minima using the Arduino IDE.

#### 2. Backend Setup

```bash
cd backend
pip install -r requirements.txt
```

#### 3. Run the Server

```bash
# Set your Arduino's serial port
export SLEEP_SERIAL_PORT=/dev/cu.usbmodem101  # macOS
# or
export SLEEP_SERIAL_PORT=COM3  # Windows

# Start the server
python3 -m uvicorn backend.server:app --reload --host 0.0.0.0 --port 8000
```

The server will be available at `http://localhost:8000/`

### Usage

- **Live Dashboard**: `http://localhost:8000/`
  - **Overview tab**: Clinician-friendly snapshot
  - **Detailed dashboard**: Charts, event log, CSV export, session reset

- **Analytics Reports**: `http://localhost:8000/reports`
  - Plotly-based trends for movement, environment, and sleep scores
  - Auto-generated notes for quick interpretation

- **Patient Navigator (demo)**: `http://localhost:8000/patients`
  - Switch between example patients for trainings or demos while the physical device remains on one bed

### API Endpoints

- `GET /` - Live dashboard
- `GET /patients` - Patient navigator (demo)
- `GET /reports` - Analytics report
- `WebSocket /stream` - Real-time data stream
- `GET /api/status` - Backend status
- `GET /api/history?limit=300` - Recent samples
- `GET /api/latest` - Most recent sample
- `POST /api/reset` - Clear data and recalibrate

### Architecture

```
Arduino (Raw Sensors 10Hz)
    ↓ Serial @ 115200 baud
Python Backend (FastAPI + SQLAlchemy)
    ├─ Real-time filtering & event detection
    ├─ SQLite persistence
    └─ WebSocket broadcast
         ↓
Web Dashboard (Chart.js)
    ├─ Overview (clinician view)
    ├─ Detailed metrics
    └─ CSV logging
```

---

## 🐾 Component 2: Cognitive Assessment (ESP32-S3 CogniPet)

Portable cognitive assessment device that performs regular cognitive tests to track cognitive function and detect delirium.

### Features

- **Cognitive Assessment Tests**: Orientation, memory, attention, executive function
- **Real-world Orientation Prompts**: When Wi‑Fi credentials are provided the device syncs its clock (Eastern/Waterloo timezone) so “What day/time is it?” questions use the actual current day and time-of-day; without Wi‑Fi it automatically falls back to the demo prompts
- **On-device Diagnostics Mode**: Hidden bedside dashboard (Wi‑Fi/NTP status, BLE queue depth, button tester, pet vitals) activated via button combo for quick troubleshooting
- **Virtual Pet Interface**: Engaging patient interaction to encourage regular check-ins
- **BLE Data Transmission**: Automatic upload of assessment and interaction data
- **Real-time Monitoring**: Track cognitive function over time
- **Alert System**: Color-coded risk levels (Green/Yellow/Orange/Red)

### Hardware Requirements

- ESP32-S3 microcontroller
- Grove LCD RGB Backlight (16x2 character LCD)
- 3 buttons (BTN1, BTN2, BTN3)
- LED for feedback

See `cognipet_esp32/README.md` for detailed hardware setup.

### Installation

#### 1. Upload Firmware

1. Open `cognipet_esp32/cognipet_esp32.ino` in Arduino IDE
2. Install ESP32 board support (see `cognipet_esp32/README.md`)
3. Select board: **ESP32S3 Dev Module**
4. Upload to your ESP32-S3

#### (Optional) Enable Wi‑Fi Time Sync

If you want the orientation questions to reference the real local date/time (Waterloo timezone), set the Wi‑Fi credentials near the top of `cognipet_esp32/cognipet_esp32.ino` before flashing:

```cpp
const char* WIFI_SSID = "YourHospitalWiFi";
const char* WIFI_PASSWORD = "super-secret";
```

On boot the device will briefly connect, synchronize against Canadian NTP servers, and then disconnect to save power. If the credentials remain as `YOUR_WIFI` / `YOUR_PASSWORD` (or Wi‑Fi is unavailable) the firmware automatically falls back to the original demo-oriented prompts.  
**Important:** the ESP32-S3 only supports **2.4 GHz** Wi‑Fi. Ensure your hotspot/router is broadcasting 2.4 GHz or mixed mode; pure 5 GHz SSIDs (e.g., “5G” hotspots) will always fail to connect.

#### 2. Start BLE Bridge

The backend server includes a BLE bridge that automatically connects to the device:

```bash
cd backend
python3 ble_bridge.py
```

Or use the restart script:
```bash
./restart_all.sh
```

### Usage

#### Button Combinations

- **Button 1 + Button 2** (hold 2 sec): Trigger cognitive assessment
- **Button 1 + Button 3** (hold 2 sec): Send test assessment data
- **Button 2 + Button 3** (hold 2 sec): Enter diagnostics mode (View Wi‑Fi/BLE status, button tester, pet vitals; hold Button 3 for ~1.5 s to exit)

#### Pet Interactions

- **Button A (BTN1)**: Feed pet
- **Button B (BTN2)**: Play mini-game
- **Button C (BTN3)**: Clean pet
- **Button C (long press)**: Open menu

### Cognitive Assessment

The device performs four cognitive tests:

1. **Orientation Test** (3 questions)
   - Day of week, time of day, location awareness

2. **Memory Test** (Simon Says style)
   - Visual sequence memory, button pattern repetition

3. **Attention Test** (Reaction time)
   - Press button when star appears, measures response time

4. **Executive Function Test** (Sequencing)
   - Order daily activities correctly

**Scoring:**
- **10-12 points**: No impairment (Green alert)
- **7-9 points**: Mild concern (Yellow alert)
- **4-6 points**: Moderate impairment (Orange alert)
- **0-3 points**: Severe impairment (Red alert)

### BLE Data Transmission

- **Device Name**: "CogniPet"
- **Service UUID**: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- **Assessment Characteristic**: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- **Interaction Characteristic**: `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e`

### API Endpoints (Cognitive Data)

- `GET /api/assessments` - Get recent cognitive assessments
- `GET /api/interactions` - Get recent pet interactions
- `POST /api/cognitive-assessment` - Receive assessment data
- `POST /api/pet-interaction` - Receive interaction data

### Architecture

```
ESP32-S3 (CogniPet Device)
    ↓ Bluetooth Low Energy (BLE)
BLE Bridge (Python)
    ↓ HTTP POST
Backend API (FastAPI)
    ↓ SQLite Database
Data Storage & Analysis
```

---

## 🚀 Quick Start (Complete System)

### Start Everything

```bash
./restart_all.sh
```

This starts:
- Backend server (http://localhost:8000)
- BLE bridge (for CogniPet)
- All services ready for both components

### Check Status

```bash
./status.sh
```

### Stop Everything

```bash
./stop_all.sh
```

---

## 📊 Data Viewing

### Sleep Monitoring Data

```bash
# View recent sleep samples
curl http://localhost:8000/api/history | python3 -m json.tool

# View latest sample
curl http://localhost:8000/api/latest | python3 -m json.tool

# Web dashboard
open http://localhost:8000/
```

### Cognitive Assessment Data

```bash
# View assessments
curl http://localhost:8000/api/assessments | python3 -m json.tool

# View interactions
curl http://localhost:8000/api/interactions | python3 -m json.tool
```

---

## 🧪 Testing

### Test Sleep Monitoring

Use the mock data generator:
```bash
cd backend
python3 mock_data_generator.py
```

### Test CogniPet Data Transmission

```bash
cd backend
python3 test_ble_data.py --direct --count 5
```

This sends test assessment data directly to the backend (bypasses BLE).

---

## 📚 Documentation

- **CogniPet Hardware Setup**: `cognipet_esp32/README.md`
- **BLE Bridge Guide**: `backend/BLE_BRIDGE_README.md`
- **Complete Setup Guide**: `SETUP_GUIDE.md`
- **Restart Guide**: `RESTART_GUIDE.md`
- **Test Guide**: `backend/TEST_GUIDE.md`

---

## 🔧 Configuration

### Sleep Monitoring Thresholds

Adjust filtering parameters in `backend/pipeline.py`:

```python
# Absolute minimum thresholds (normalized RMS units)
self.config = {
  "head": ChannelConfig(0.25, 1.20, 1.5),
  "body": ChannelConfig(1.00, 0.90, 6.0),
  "leg": ChannelConfig(1.00, 1.00, 5.5),
}

# Activity classification
value >= 1.0  → Needs attention
value >= 0.5  → Slight movement  
value < 0.5   → Idle
```

---

## 🛠️ Troubleshooting

### Sleep Monitoring Issues

- **No data received**: Check serial port connection
- **Data looks wrong**: Verify sensor connections
- **Dashboard not updating**: Check WebSocket connection

### CogniPet Issues

- **Device not found**: Ensure ESP32 is powered on, Bluetooth enabled
- **Data not appearing**: Check BLE bridge is connected
- **Assessment not working**: Check Serial Monitor for errors

See individual component documentation for detailed troubleshooting.

---

## 📦 Dependencies

### Python Backend

See `backend/requirements.txt`:
- FastAPI
- Uvicorn
- SQLAlchemy
- Bleak (BLE library)
- Plotly
- Requests

### Arduino/ESP32

- ESP32 Board Support Package (for CogniPet)
- Standard Arduino libraries (for NightWatch)

---

## 🎯 Use Cases

- **Hospital Patient Monitoring**: Combined sleep and cognitive monitoring for delirium detection
- **Early Warning System**: Detect delirium risk before symptoms become severe
- **Long-term Care**: Track sleep patterns and cognitive function over time
- **Research**: Collect comprehensive patient data for delirium studies
- **Rehabilitation**: Monitor recovery progress and cognitive improvement

---

## 🔗 Links

- **Repository**: https://github.com/Mayalevich/NightWatch.git
- **Backend API**: http://localhost:8000
- **API Docs**: http://localhost:8000/docs (when server is running)

---

## 📄 License

MIT

---

## 🤝 Contributing

This is a research/educational project. Contributions welcome!

---

**Built for hospital-induced delirium detection and prevention** 🏥
