#!/bin/bash
# Restart script for CogniPet system
# Use this when you unplug/replug your ESP32

echo "=== CogniPet System Restart ==="
echo ""

# Kill existing processes
echo "Stopping existing services..."
pkill -f "uvicorn.*server:app" 2>/dev/null
pkill -f "ble_bridge" 2>/dev/null
sleep 2

# Clear port 8000 if needed
lsof -ti:8000 | xargs kill -9 2>/dev/null
sleep 1

# Start backend server
echo "Starting backend server..."
cd "$(dirname "$0")"
# Run from project root, not backend directory
python3 -m uvicorn backend.server:app --reload --host 0.0.0.0 --port 8000 > /tmp/cognipet_backend.log 2>&1 &
BACKEND_PID=$!
echo "  Waiting for backend to start (PID: $BACKEND_PID)..."
sleep 5

# Check if backend started (try multiple times)
MAX_RETRIES=5
RETRY=0
BACKEND_STARTED=false
while [ $RETRY -lt $MAX_RETRIES ]; do
    if curl -s http://localhost:8000/status > /dev/null 2>&1; then
        echo "✓ Backend server started (PID: $BACKEND_PID)"
        BACKEND_STARTED=true
        break
    fi
    RETRY=$((RETRY + 1))
    echo "  Retrying... ($RETRY/$MAX_RETRIES)"
    sleep 2
done

if [ "$BACKEND_STARTED" = false ]; then
    echo "✗ Backend server failed to start"
    echo "  Check logs: tail -20 /tmp/cognipet_backend.log"
    tail -20 /tmp/cognipet_backend.log
    exit 1
fi

# Start BLE bridge
echo "Starting BLE bridge..."
cd backend
python3 ble_bridge.py > /tmp/ble_bridge.log 2>&1 &
BRIDGE_PID=$!
cd ..
sleep 3

# Check if bridge started
if ps -p $BRIDGE_PID > /dev/null 2>&1; then
    echo "✓ BLE bridge started (PID: $BRIDGE_PID)"
    echo "  Scanning for CogniPet device..."
else
    echo "✗ BLE bridge failed to start"
    echo "  Check logs: tail -20 /tmp/ble_bridge.log"
    tail -20 /tmp/ble_bridge.log
    exit 1
fi

echo ""
echo "=== System Status ==="
echo "Backend: http://localhost:8000"
echo "Backend Log: tail -f /tmp/cognipet_backend.log"
echo "Bridge Log: tail -f /tmp/ble_bridge.log"
if [ -f "backend/sleep_data.db" ]; then
  CLOCK_STATUS=$(cd backend && python3 - <<'PY'
import sqlite3, time, datetime
conn = sqlite3.connect('sleep_data.db')
cur = conn.cursor()
cur.execute('SELECT device_timestamp_ms, recorded_at FROM cognitive_assessments ORDER BY recorded_at DESC LIMIT 1')
row = cur.fetchone()
conn.close()

if not row or row[0] is None:
    print("Clock Sync: No assessments yet")
else:
    ts = int(row[0])
    if ts > 1_000_000_000:
        now = time.time()
        delta = abs(now - ts)
        human = datetime.datetime.fromtimestamp(ts).strftime('%Y-%m-%d %H:%M:%S')
        if delta < 600:
            print(f"Clock Sync: ✓ Wi-Fi/NTP (last {human}, Δ{delta:.0f}s)")
        else:
            print(f"Clock Sync: ! Stale epoch ({human}, Δ{delta:.0f}s)")
    else:
        minutes = ts / 60000 if ts else 0
        print(f"Clock Sync: ✗ Using uptime counter ({ts} ms ≈ {minutes:.1f} min)")
PY
)
  echo "$CLOCK_STATUS"
else
  echo "Clock Sync: Database not created yet"
fi
echo ""
echo "Launching dashboard in your default browser..."
if command -v open >/dev/null 2>&1; then
  open "http://localhost:8000" >/dev/null 2>&1 &
elif command -v xdg-open >/dev/null 2>&1; then
  xdg-open "http://localhost:8000" >/dev/null 2>&1 &
elif command -v start >/dev/null 2>&1; then
  start "http://localhost:8000" >/dev/null 2>&1 &
else
  echo "  (Could not auto-open browser; please visit http://localhost:8000 manually)"
fi
echo ""
echo "✓ All services started!"
echo ""
echo "Next steps:"
echo "1. Power on your ESP32-S3"
echo "2. Wait 5-10 seconds for it to boot"
echo "3. The bridge will automatically connect"
echo "4. Check connection: tail -f /tmp/ble_bridge.log"
echo ""
echo "To stop all services: ./stop_all.sh"

