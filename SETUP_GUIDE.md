# Step-by-Step Guide: Setting Up BLE Data Upload

This guide will walk you through setting up the complete system to upload data from your CogniPet ESP32-S3 device to the backend via Bluetooth.

## Prerequisites

- ESP32-S3 with CogniPet firmware uploaded
- Computer with Bluetooth capability
- Python 3.7+ installed
- Backend server code in this repository

---

## Step 1: Install Python Dependencies

Open a terminal and navigate to the project directory:

```bash
cd /Users/jingyu/Documents/Arduino/sketch_nov2a
```

Install the required Python packages:

```bash
cd backend
pip install -r requirements.txt
```

**What this does:** Installs `bleak` (for Bluetooth), `requests` (for HTTP), and other dependencies.

**Expected output:** You should see packages being installed. If you get permission errors, try:
```bash
pip install --user -r requirements.txt
```

---

## Step 2: Initialize the Database

The database will be created automatically when the server starts, but let's verify the database models are ready:

```bash
python -c "from database import init_db; init_db(); print('Database initialized!')"
```

**What this does:** Creates the SQLite database file (`sleep_data.db`) with tables for assessments and interactions.

**Expected output:** `Database initialized!`

---

## Step 3: Start the Backend Server

In a **new terminal window** (keep this running), start the FastAPI server:

```bash
cd /Users/jingyu/Documents/Arduino/sketch_nov2a/backend
python -m uvicorn server:app --reload --host 0.0.0.0 --port 8000
```

**What this does:** Starts the web server that will receive data from the BLE bridge.

**Expected output:**
```
INFO:     Uvicorn running on http://0.0.0.0:8000 (Press CTRL+C to quit)
INFO:     Started reloader process
INFO:     Started server process
INFO:     Waiting for application startup.
INFO:     Application startup complete.
```

**Keep this terminal window open!** The server needs to keep running.

---

## Step 4: Verify Backend is Running

Open a **third terminal window** and test the backend:

```bash
curl http://localhost:8000/status
```

**Expected output:** `{"status":"ok"}`

Or open in your browser: http://localhost:8000/status

---

## Step 5: Power On Your ESP32-S3 Device

1. Connect your ESP32-S3 to power (USB or battery)
2. Wait for it to boot (you should see the LCD display)
3. The device will start advertising as "CogniPet" via Bluetooth
4. *(Optional)* If you configured Wi‑Fi credentials in the firmware, the device will briefly connect and sync its clock to Waterloo time before advertising

**Note:** Make sure the device is within Bluetooth range (typically 10-30 meters).

---

## Step 6: Run the BLE Bridge

In a **new terminal window** (or use the one from Step 4), run the bridge:

```bash
cd /Users/jingyu/Documents/Arduino/sketch_nov2a/backend
python ble_bridge.py
```

**What this does:** 
- Scans for your "CogniPet" device
- Connects via Bluetooth
- Subscribes to data notifications
- Forwards data to the backend API

**Expected output:**
```
Scanning for BLE device: CogniPet
Backend URL: http://localhost:8000
Press Ctrl+C to stop

Found device: CogniPet (XX:XX:XX:XX:XX:XX)
Connected to CogniPet
Subscribing to characteristics...
✓ Subscribed to assessment characteristic
✓ Subscribed to interaction characteristic

==================================================
Bridge is running. Waiting for data...
==================================================
```

**Keep this terminal window open!** The bridge needs to keep running.

---

## Step 7: Trigger Data on Your Device

Now interact with your CogniPet device to generate data:

### Option A: Trigger Cognitive Assessment (Backdoor)
1. On your ESP32-S3, **hold Button 1 + Button 2 simultaneously for 2 seconds**
2. The assessment will start
3. Complete the assessment by answering questions

### Option B: Interact with the Pet
1. Use the buttons to feed, play, or clean the pet
2. Navigate menus and play games
3. Each interaction will be logged

---

## Step 8: Verify Data is Being Received

You should see output in the **BLE bridge terminal** (Step 6):

```
[Assessment] Received 32 bytes
  Score: 8/12 (O:2 M:2 A:2 E:2)
  Alert Level: 1
✓ Data sent to backend: {'device_timestamp_ms': 12345, ...}

[Interaction] Received 9 bytes
  Type: feed, Success: True, Time: 450ms
✓ Data sent to backend: {'device_timestamp_ms': 12346, ...}
```

---

## Step 9: View Data in Backend

### Option A: Via API (Terminal)

**View recent assessments:**
```bash
curl http://localhost:8000/api/assessments | python -m json.tool
```

**View recent interactions:**
```bash
curl http://localhost:8000/api/interactions | python -m json.tool
```

### Option B: Via Web Browser

Open your browser and visit:
- **Dashboard:** http://localhost:8000/
- **Assessments API:** http://localhost:8000/api/assessments
- **Interactions API:** http://localhost:8000/api/interactions

---

## Step 10: Test Complete Workflow

1. **Trigger an assessment** on your device (hold A+B for 2 seconds)
2. **Complete the assessment** (answer all questions)
3. **Check the bridge terminal** - you should see assessment data
4. **Check the backend** - query `/api/assessments` to see the data
5. **Interact with the pet** (feed, play, clean)
6. **Check interactions** - query `/api/interactions` to see the data

---

## Troubleshooting

### Problem: "Device not found"
**Solution:**
- Make sure ESP32-S3 is powered on
- Check Bluetooth is enabled on your computer
- Try moving the device closer
- Verify device name is "CogniPet" (check Serial Monitor)

### Problem: "Connection failed"
**Solution:**
- Restart the ESP32-S3 device
- Restart the BLE bridge
- Check Bluetooth permissions (macOS: System Preferences > Security & Privacy)

### Problem: "Backend connection failed"
**Solution:**
- Verify backend server is running (Step 3)
- Check the URL: `curl http://localhost:8000/status`
- If backend is on another machine, use: `python ble_bridge.py --backend-url http://IP:8000`

### Problem: "No data appearing"
**Solution:**
- Check ESP32-S3 Serial Monitor to verify data is being sent
- Verify bridge is connected (should show "Connected to CogniPet")
- Check backend logs for errors
- Try triggering assessment again

### Problem: "Permission denied" when installing packages
**Solution:**
```bash
pip install --user -r requirements.txt
```
Or use a virtual environment:
```bash
python -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
pip install -r requirements.txt
```

---

## Running Everything Together

You need **3 terminal windows** running simultaneously:

1. **Terminal 1:** Backend server
   ```bash
   cd backend && python -m uvicorn server:app --reload --host 0.0.0.0 --port 8000
   ```

2. **Terminal 2:** BLE bridge
   ```bash
   cd backend && python ble_bridge.py
   ```

3. **Terminal 3:** For testing/commands
   ```bash
   curl http://localhost:8000/api/assessments
   ```

---

## Next Steps

Once everything is working:
- Data will automatically flow from device → bridge → backend → database
- You can query the API anytime to view data
- The database persists data between restarts
- You can build dashboards or analysis tools on top of the API

---

## Quick Reference

**Start backend:**
```bash
cd backend && python -m uvicorn server:app --reload --host 0.0.0.0 --port 8000
```

**Start bridge:**
```bash
cd backend && python ble_bridge.py
```

**View assessments:**
```bash
curl http://localhost:8000/api/assessments
```

**View interactions:**
```bash
curl http://localhost:8000/api/interactions
```

**Stop everything:**
- Press `Ctrl+C` in each terminal window

