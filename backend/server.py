import asyncio
import csv
import json
import logging
import os
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional

import serial
from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse
from fastapi.templating import Jinja2Templates

import plotly.graph_objs as go
import plotly.io as pio
from plotly.subplots import make_subplots

from .pipeline import RawSample, SleepPipeline
from . import database

logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger("sleep-backend")


class BroadcastHub:
  def __init__(self) -> None:
    self.queues: List[asyncio.Queue] = []
    self._lock = asyncio.Lock()

  async def register(self) -> asyncio.Queue:
    queue: asyncio.Queue = asyncio.Queue(maxsize=1)
    async with self._lock:
      self.queues.append(queue)
    return queue

  async def unregister(self, queue: asyncio.Queue) -> None:
    async with self._lock:
      if queue in self.queues:
        self.queues.remove(queue)

  async def publish(self, message: Dict) -> None:
    async with self._lock:
      queues = list(self.queues)
    for queue in queues:
      if queue.full():
        try:
          queue.get_nowait()
        except asyncio.QueueEmpty:
          pass
      await queue.put(message)


class SerialReader:
  def __init__(self, port: str, baud: int = 115200) -> None:
    self.port = port
    self.baud = baud
    self._serial: Optional[serial.Serial] = None

  def open(self) -> None:
    if self._serial and self._serial.is_open:
      return
    logger.info("Opening serial port %s", self.port)
    self._serial = serial.Serial(self.port, self.baud, timeout=1)
    self._serial.reset_input_buffer()
    time.sleep(0.1)

  def readline(self) -> Optional[str]:
    if not self._serial:
      return None
    while True:
      try:
        raw = self._serial.readline()
      except serial.SerialException as exc:
        logger.debug("Serial transient: %s", exc)
        return None
      if not raw:
        return None
      decoded = raw.decode("utf-8", errors="ignore").strip()
      if decoded:
        return decoded

  def close(self) -> None:
    if self._serial and self._serial.is_open:
      logger.info("Closing serial port")
      self._serial.close()


class BackendService:
  def __init__(self, port: str) -> None:
    self.pipeline = SleepPipeline()
    self.reader = SerialReader(port)
    self.hub = BroadcastHub()
    self._task: Optional[asyncio.Task] = None
    self._running = False

  async def start(self) -> None:
    if self._task and not self._task.done():
      return
    self._running = True
    loop = asyncio.get_running_loop()
    self.reader.open()
    self._task = loop.create_task(self._run())

  async def stop(self) -> None:
    self._running = False
    if self._task:
      await self._task
    self.reader.close()

  async def reset(self) -> None:
    self.pipeline = SleepPipeline()
    await asyncio.to_thread(database.clear_samples)
    await self.hub.publish({"type": "status", "message": "reset"})

  async def _run(self) -> None:
    header_seen = False
    while self._running:
      line = await asyncio.to_thread(self.reader.readline)
      if not line:
        continue
      logger.info("Serial line: %s", line)
      if not header_seen:
        if line.startswith("time_ms"):
          header_seen = True
          logger.debug("CSV header detected")
          continue
      sample = self._parse_sample(line)
      if not sample:
        logger.warning("Failed to parse sample from line: %s", line)
        continue
      logger.debug("Parsed sample: time_ms=%d", sample.time_ms)
      events = self.pipeline.process_sample(sample)
      logger.debug("Pipeline returned %d events", len(events))
      for event in events:
        if event.get("type") == "data":
          processed = event["payload"]
          logger.info("Saving processed sample: time_s=%.1f", processed.time_s)
          await asyncio.to_thread(database.save_processed_sample, processed)
          logger.info("Sample saved to database")
        await self.hub.publish(event)

  def _parse_sample(self, line: str) -> Optional[RawSample]:
    try:
      parts = next(csv.reader([line]))
      if len(parts) < 8:
        return None
      time_ms = int(parts[0])
      piezo = {
        "head": int(parts[1]),
        "body": int(parts[2]),
        "leg": int(parts[3]),
      }
      sound = int(parts[4])
      light = int(parts[5])
      temp = int(parts[6])
      button = int(parts[7])
      return RawSample(
        time_ms=time_ms,
        piezo=piezo,
        sound=sound,
        light=light,
        temp_raw=temp,
        button=button,
      )
    except (ValueError, csv.Error) as exc:
      logger.warning("Failed to parse line '%s': %s", line, exc)
      return None


SERIAL_PORT = os.environ.get("SLEEP_SERIAL_PORT", "/dev/tty.usbmodem1101")
service = BackendService(SERIAL_PORT)
BASE_DIR = Path(__file__).resolve().parent
DASHBOARD_PATH = BASE_DIR.parent / "dashboard.html"
templates = Jinja2Templates(directory=str(BASE_DIR / "templates"))

app = FastAPI(title="Sleep Monitor Backend", version="1.0.0")
app.add_middleware(
  CORSMiddleware,
  allow_origins=["*"],
  allow_credentials=True,
  allow_methods=["*"],
  allow_headers=["*"],
)


@app.on_event("startup")
async def startup_event() -> None:
  database.init_db()
  try:
    await service.start()
  except Exception as exc:  # pragma: no cover
    logger.error("Failed to start backend service: %s", exc)


@app.on_event("shutdown")
async def shutdown_event() -> None:
  await service.stop()


@app.get("/status")
async def status() -> JSONResponse:
  latest = await asyncio.to_thread(database.get_latest_sample)
  total_rows = await asyncio.to_thread(database.count_samples)
  body = {
    "monitoring": service.pipeline.monitoring,
    "calibrating": service.pipeline.calibrating,
    "serial_port": SERIAL_PORT,
    "rows_recorded": total_rows,
    "latest": latest,
  }
  return JSONResponse(body)


@app.websocket("/stream")
async def stream(websocket: WebSocket) -> None:
  await websocket.accept()
  queue = await service.hub.register()
  try:
    while True:
      event = await queue.get()
      if event.get("type") == "data":
        payload = event["payload"].to_dict()
        await websocket.send_text(json.dumps({"type": "data", "payload": payload}))
      elif event.get("type") == "status":
        await websocket.send_text(json.dumps(event))
  except WebSocketDisconnect:
    logger.info("WebSocket disconnected")
  finally:
    await service.hub.unregister(queue)


@app.get("/")
async def index() -> FileResponse:
  return FileResponse(DASHBOARD_PATH)


@app.get("/api/history")
async def history(limit: int = 300, minutes: Optional[int] = None) -> JSONResponse:
  if minutes is not None and minutes > 0:
    data = await asyncio.to_thread(database.get_samples_since, minutes)
  else:
    data = await asyncio.to_thread(database.get_recent_samples, max(1, min(limit, 2000)))
  return JSONResponse(data)


@app.get("/api/latest")
async def latest() -> JSONResponse:
  data = await asyncio.to_thread(database.get_latest_sample)
  if data is None:
    return JSONResponse({}, status_code=204)
  return JSONResponse(data)


@app.post("/api/reset")
async def reset() -> JSONResponse:
  await service.reset()
  return JSONResponse({"status": "reset"})


def _parse_timestamp(payload: Dict) -> datetime:
  raw = payload.get("recorded_at")
  if raw:
    try:
      return datetime.fromisoformat(raw)
    except ValueError:
      pass
  return datetime.now(timezone.utc)


def _score_hint(score: float) -> str:
  if score >= 85:
    return "Excellent rest"
  if score >= 70:
    return "Generally restful"
  if score >= 50:
    return "Monitor for restlessness"
  return "High movement detected"


@app.get("/reports", response_class=HTMLResponse)
async def reports(request: Request, minutes: int = 180) -> HTMLResponse:
  window_minutes = max(30, min(minutes, 1440))
  rows = await asyncio.to_thread(database.get_samples_since, window_minutes)

  if not rows:
    context = {
      "request": request,
      "window_minutes": window_minutes,
      "avg_score": "–",
      "score_hint": "No data recorded yet",
      "events_total": "–",
      "events_head": "–",
      "events_body": "–",
      "events_leg": "–",
      "temp_avg": "–",
      "light_avg": "–",
      "sound_avg": "–",
      "sample_count": 0,
      "notes": ["Connect a device to start capturing data."],
      "plot_movement": "<div style='padding:40px; text-align:center; color:#64748b;'>No samples available</div>",
      "plot_heatmap": "<div style='padding:40px; text-align:center; color:#64748b;'>No samples available</div>",
      "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
    }
    return templates.TemplateResponse("report.html", context)

  times: List[datetime] = []
  rms_head: List[float] = []
  rms_body: List[float] = []
  rms_leg: List[float] = []
  temps: List[float] = []
  lights: List[int] = []
  sounds: List[float] = []
  scores: List[float] = []
  events_min_head: List[int] = []
  events_min_body: List[int] = []
  events_min_leg: List[int] = []
  total_events_min: List[int] = []

  for payload in rows:
    ts = _parse_timestamp(payload)
    times.append(ts)
    rms_head.append(float(payload.get("RMS_H", 0.0)))
    rms_body.append(float(payload.get("RMS_B", 0.0)))
    rms_leg.append(float(payload.get("RMS_L", 0.0)))
    temps.append(float(payload.get("TempC", 0.0)))
    lights.append(int(payload.get("LightRaw", 0)))
    sounds.append(float(payload.get("SoundRMS", 0.0)))
    scores.append(float(payload.get("SleepScore", 0.0)))
    events_min_head.append(int(payload.get("minute_events_H", 0)))
    events_min_body.append(int(payload.get("minute_events_B", 0)))
    events_min_leg.append(int(payload.get("minute_events_L", 0)))
    total_events_min.append(int(payload.get("total_events_min", 0)))

  fig = make_subplots(
    rows=2,
    cols=1,
    shared_xaxes=True,
    vertical_spacing=0.12,
    subplot_titles=("Regional Movement (RMS)", "Environment (Temp / Light / Sound)"),
  )

  fig.add_trace(go.Scatter(x=times, y=rms_head, name="Head RMS", line=dict(color="#4338ca", width=2)), row=1, col=1)
  fig.add_trace(go.Scatter(x=times, y=rms_body, name="Body RMS", line=dict(color="#14b8a6", width=2)), row=1, col=1)
  fig.add_trace(go.Scatter(x=times, y=rms_leg, name="Leg RMS", line=dict(color="#f97316", width=2)), row=1, col=1)

  fig.add_trace(go.Scatter(x=times, y=temps, name="Temp °C", line=dict(color="#0ea5e9", width=2)), row=2, col=1)
  fig.add_trace(go.Scatter(x=times, y=lights, name="Light", line=dict(color="#facc15", width=1.6, dash="dot")), row=2, col=1)
  fig.add_trace(go.Scatter(x=times, y=sounds, name="Sound RMS", line=dict(color="#22d3ee", width=1.8, dash="dash")), row=2, col=1)

  fig.update_layout(
    margin=dict(l=50, r=20, t=60, b=40),
    legend=dict(orientation="h", y=1.15, x=0),
    plot_bgcolor="#ffffff",
    paper_bgcolor="#ffffff",
    font=dict(family="Inter, sans-serif", size=13),
  )
  fig.update_xaxes(showgrid=True, gridcolor="#e2e8f0")
  fig.update_yaxes(showgrid=True, gridcolor="#e2e8f0")

  plot_movement = pio.to_html(fig, include_plotlyjs="cdn", full_html=False, config={"displayModeBar": False})

  heatmap = go.Figure(
    data=go.Heatmap(
      z=[scores],
      x=[t.strftime("%H:%M") for t in times],
      y=["Sleep Score"],
      colorscale="Blues",
      zmin=0,
      zmax=100,
      hovertemplate="Time %{x}<br>Score %{z:.1f}<extra></extra>",
    )
  )
  heatmap.update_layout(
    margin=dict(l=40, r=20, t=20, b=40),
    plot_bgcolor="#ffffff",
    paper_bgcolor="#ffffff",
    font=dict(family="Inter, sans-serif", size=13),
  )
  plot_heatmap = pio.to_html(heatmap, include_plotlyjs=False, full_html=False, config={"displayModeBar": False})

  sample_count = len(rows)
  avg_score = sum(scores) / sample_count if sample_count else 0.0
  avg_events_total = sum(total_events_min) / sample_count if sample_count else 0.0
  avg_events_head = sum(events_min_head) / sample_count if sample_count else 0.0
  avg_events_body = sum(events_min_body) / sample_count if sample_count else 0.0
  avg_events_leg = sum(events_min_leg) / sample_count if sample_count else 0.0
  avg_temp = sum(temps) / sample_count if sample_count else 0.0
  avg_light = sum(lights) / sample_count if sample_count else 0.0
  avg_sound = sum(sounds) / sample_count if sample_count else 0.0

  notes: List[str] = []
  notes.append(_score_hint(avg_score))
  if avg_events_total >= 10:
    notes.append("High overall activity")
  elif avg_events_total >= 5:
    notes.append("Moderate nightly activity")
  else:
    notes.append("Low activity baseline")

  if avg_temp < 18:
    notes.append("Room trending cool")
  elif avg_temp > 26:
    notes.append("Room trending warm")

  context = {
    "request": request,
    "window_minutes": window_minutes,
    "avg_score": f"{avg_score:.1f}",
    "score_hint": _score_hint(avg_score),
    "events_total": f"{avg_events_total:.1f}",
    "events_head": f"{avg_events_head:.1f}",
    "events_body": f"{avg_events_body:.1f}",
    "events_leg": f"{avg_events_leg:.1f}",
    "temp_avg": f"{avg_temp:.1f}",
    "light_avg": f"{avg_light:.0f}",
    "sound_avg": f"{avg_sound:.1f}",
    "sample_count": sample_count,
    "notes": notes,
    "plot_movement": plot_movement,
    "plot_heatmap": plot_heatmap,
    "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
  }
  return templates.TemplateResponse("report.html", context)
