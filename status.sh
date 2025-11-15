#!/bin/bash
# Check status of all CogniPet services

echo "=== CogniPet System Status ==="
echo ""

# Check backend
if curl -s http://localhost:8000/status > /dev/null 2>&1; then
    echo "✓ Backend Server: Running on http://localhost:8000"
    BACKEND_PID=$(ps aux | grep "[u]vicorn.*server:app" | awk '{print $2}' | head -1)
    echo "  PID: $BACKEND_PID"
else
    echo "✗ Backend Server: Not running"
fi

# Check BLE bridge
BRIDGE_PID=$(ps aux | grep "[b]le_bridge" | awk '{print $2}' | head -1)
if [ -n "$BRIDGE_PID" ]; then
    echo "✓ BLE Bridge: Running (PID: $BRIDGE_PID)"
    echo "  Log: tail -f /tmp/ble_bridge.log"
else
    echo "✗ BLE Bridge: Not running"
fi

# Check database
if [ -f "backend/sleep_data.db" ]; then
    DB_SIZE=$(du -h backend/sleep_data.db | cut -f1)
    echo "✓ Database: Exists ($DB_SIZE)"
    
    # Count records
    ASSESSMENTS=$(cd backend && python3 -c "import sqlite3; conn = sqlite3.connect('sleep_data.db'); cursor = conn.cursor(); cursor.execute('SELECT COUNT(*) FROM cognitive_assessments'); print(cursor.fetchone()[0]); conn.close()" 2>/dev/null)
    INTERACTIONS=$(cd backend && python3 -c "import sqlite3; conn = sqlite3.connect('sleep_data.db'); cursor = conn.cursor(); cursor.execute('SELECT COUNT(*) FROM pet_interactions'); print(cursor.fetchone()[0]); conn.close()" 2>/dev/null)
    
    echo "  Assessments: $ASSESSMENTS"
    echo "  Interactions: $INTERACTIONS"

    CLOCK_STATUS=$(cd backend && python3 - <<'PY'
import sqlite3, time, datetime
conn = sqlite3.connect('sleep_data.db')
cur = conn.cursor()
cur.execute('SELECT device_timestamp_ms FROM cognitive_assessments ORDER BY recorded_at DESC LIMIT 1')
row = cur.fetchone()
conn.close()

if not row or row[0] is None:
    print("  Clock Sync: No assessments yet")
else:
    ts = int(row[0])
    if ts > 1_000_000_000:  # looks like epoch seconds
        now = time.time()
        delta = abs(now - ts)
        human = datetime.datetime.fromtimestamp(ts).strftime('%Y-%m-%d %H:%M:%S')
        if delta < 600:
            print(f"  Clock Sync: ✓ Wi-Fi/NTP (last {human}, Δ{delta:.0f}s)")
        else:
            print(f"  Clock Sync: ! Stale epoch ({human}, Δ{delta:.0f}s)")
    else:
        minutes = ts / 60000 if ts else 0
        print(f"  Clock Sync: ✗ Using uptime counter ({ts} ms ≈ {minutes:.1f} min)")
PY
)
    echo "$CLOCK_STATUS"
else
    echo "✗ Database: Not found"
fi

echo ""
echo "=== Quick Commands ==="
echo "Restart all: ./restart_all.sh"
echo "Stop all: ./stop_all.sh"
echo "View assessments: curl http://localhost:8000/api/assessments | python3 -m json.tool"
echo "View interactions: curl http://localhost:8000/api/interactions | python3 -m json.tool"

