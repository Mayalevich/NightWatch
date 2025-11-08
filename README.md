# NightWatch

Real-time sleep monitoring system with Arduino sensor integration and intelligent movement detection.

## Overview

NightWatch is a comprehensive sleep monitoring solution that captures piezoelectric sensor data from head, body, and leg regions, along with environmental metrics (sound, light, temperature). It features:

- **Real-time monitoring** via WebSocket streaming
- **Intelligent filtering** with adaptive thresholds and EMA smoothing
- **3-level activity classification** (Idle / Slight movement / Needs attention)
- **Dual interface**: Clinician-friendly overview + detailed technical dashboard
- **Data persistence** with SQLite database
- **Advanced analytics** with Plotly visualizations

## Hardware Requirements

- Arduino UNO R4 Minima
- 3× Piezo sensors (Head: A3, Body: A4, Leg: A5)
- Sound sensor (A0)
- Light sensor (A1)
- Temperature sensor (A2)
- Push button (D2 to GND)

## Installation

### 1. Arduino Setup

Upload `sketch_nov2a.ino` to your Arduino UNO R4 Minima using the Arduino IDE.

### 2. Backend Setup

```bash
cd /path/to/NightWatch
python3 -m venv .venv
source .venv/bin/activate  # On Windows: .venv\Scripts\activate
pip install -r backend/requirements.txt
```

### 3. Run the Server

```bash
# Set your Arduino's serial port
export SLEEP_SERIAL_PORT=/dev/cu.usbmodem101  # macOS
# or
export SLEEP_SERIAL_PORT=COM3  # Windows

# Start the server
uvicorn backend.server:app
```

The server will be available at `http://localhost:8000/`

## Usage

### Live Dashboard
- Navigate to `http://localhost:8000/`
- **Overview tab**: Quick status for clinicians/nurses
- **Detailed Dashboard tab**: Full metrics, charts, and event logs
- **Clear Data button**: Reset session and recalibrate

### Analytics Reports
- Visit `http://localhost:8000/reports` for historical analysis
- Interactive Plotly charts showing trends
- Summary statistics and environmental data

### Data Export
- **Start Logging**: Begin CSV recording
- **Download CSV**: Export logged session data
- **Stream to File**: Direct-to-disk streaming (Chrome/Edge)

## API Endpoints

- `GET /` - Live dashboard
- `GET /reports` - Analytics report
- `WebSocket /stream` - Real-time data stream
- `GET /api/status` - Backend status
- `GET /api/history?limit=300` - Recent samples
- `GET /api/latest` - Most recent sample
- `POST /api/reset` - Clear data and recalibrate

## Architecture

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

## Configuration

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

## Development

Run with auto-reload (requires filesystem permissions):
```bash
uvicorn backend.server:app --reload
```

## Future Enhancements

- Sleep pose detection model integration
- Multi-session comparison
- Alert notifications
- Mobile app

## License

MIT

## Contributors

Sleep monitoring suite for healthcare applications.

