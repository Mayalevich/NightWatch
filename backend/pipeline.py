import math
import logging
from dataclasses import dataclass
from typing import Dict, List, Optional

logger = logging.getLogger("sleep-backend")


@dataclass
class ChannelConfig:
  norm_gain: float
  region_mult: float
  abs_move_thresh: float


@dataclass
class ChannelState:
  config: ChannelConfig
  baseline: float = 0.0
  noise_ema: float = 0.0
  vib_thresh: float = 0.0
  prev_rms_raw: float = 0.0
  moving: bool = False
  acc2: float = 0.0
  count: int = 0
  rms: float = 0.0
  rms_filtered: float = 0.0
  events_sec: int = 0
  events_min: int = 0
  activity_level: int = 0


@dataclass
class RawSample:
  time_ms: int
  piezo: Dict[str, int]
  sound: int
  light: int
  temp_raw: int
  button: int


@dataclass
class ProcessedSample:
  time_s: float
  rms: Dict[str, float]
  moving: Dict[str, bool]
  events_sec: Dict[str, int]
  events_min: Dict[str, int]
  total_events_min: int
  sleep_score: float
  sound_rms: float
  light_avg: int
  temp_c: float
  monitoring: bool
  activity: Dict[str, int]
  status_level: int
  status_label: str

  def to_dict(self) -> Dict[str, float]:
    return {
      "time_s": self.time_s,
      "RMS_H": self.rms["head"],
      "RMS_B": self.rms["body"],
      "RMS_L": self.rms["leg"],
      "MOV_H": 1 if self.moving["head"] else 0,
      "MOV_B": 1 if self.moving["body"] else 0,
      "MOV_L": 1 if self.moving["leg"] else 0,
      "secEv_H": self.events_sec["head"],
      "secEv_B": self.events_sec["body"],
      "secEv_L": self.events_sec["leg"],
      "minute_events_H": self.events_min["head"],
      "minute_events_B": self.events_min["body"],
      "minute_events_L": self.events_min["leg"],
      "total_events_min": self.total_events_min,
      "SleepScore": self.sleep_score,
      "SoundRMS": self.sound_rms,
      "LightRaw": self.light_avg,
      "TempC": self.temp_c,
      "monitoring": 1 if self.monitoring else 0,
      "activity_H": self.activity["head"],
      "activity_B": self.activity["body"],
      "activity_L": self.activity["leg"],
      "status_level": self.status_level,
      "status_label": self.status_label,
    }


class SleepPipeline:
  CHANNELS = ("head", "body", "leg")

  def __init__(self):
    # Adjusted thresholds based on observed data: head p95=1.06, body p95=5.61, leg p95=5.03
    self.config = {
      "head": ChannelConfig(0.25, 1.00, 2.0),   # more conservative
      "body": ChannelConfig(0.70, 1.00, 8.0),   # reduced norm_gain to suppress constant ~1.0 baseline
      "leg": ChannelConfig(1.00, 1.00, 6.0),    # increased slightly
    }
    self.gain_vib = 6.0
    self.vib_margin_add = 10.0  # reduced margin for tighter thresholds
    self.noise_alpha = 0.05  # faster EMA adaptation
    self.hyst_add = 2.0  # tighter hysteresis
    self.spike_factor = 3.0  # less aggressive spike suppression
    self.rms_window_ms = 250
    self.second_window_ms = 1000
    self.minute_window_ms = 60000
    self.button_debounce_ms = 40
    self.button_long_press_ms = 2000
    self.vref = 5.0
    self.adc_max = 1023
    
    # Improved filtering
    self.filter_alpha = 0.25  # EMA smoothing for RMS (higher = more responsive)
    
    # Activity level thresholds (ratio to dynamic threshold)
    self.slight_thresh = 0.7   # above this: slight movement
    self.attention_thresh = 1.5  # above this: needs attention

    self.channels: Dict[str, ChannelState] = {
      name: ChannelState(self.config[name]) for name in self.CHANNELS
    }

    self.monitoring = True
    self.button_down = False
    self.button_press_start: Optional[int] = None

    self.last_rms_ms: Optional[int] = None
    self.last_second_ms: Optional[int] = None
    self.last_minute_ms: Optional[int] = None
    self.start_time_ms: Optional[int] = None

    self.snd_acc2 = 0.0
    self.snd_cnt = 0
    self.light_sum = 0
    self.light_cnt = 0
    self.temp_sum = 0
    self.temp_cnt = 0

    self.sound_thresh = 0.0

    self.calibrating = True
    self.calibration_started_ms: Optional[int] = None
    self.calibration_sample_target = 30  # collect 30 samples (~3 seconds at 10Hz)
    self.calibration_samples: Dict[str, List[int]] = {name: [] for name in self.CHANNELS}
    self.sound_calibration: List[int] = []
    self._calibration_announced = False

  def reset_calibration(self):
    self.calibrating = True
    self.calibration_started_ms = None
    for values in self.calibration_samples.values():
      values.clear()
    self.sound_calibration.clear()
    self._calibration_announced = False

  def _adc_to_temp_c(self, adc: float) -> float:
    v = (adc * self.vref) / self.adc_max
    return (v - 0.5) * 100.0

  def process_sample(self, sample: RawSample) -> List[Dict]:
    events: List[Dict] = []

    if self.calibrating:
      self._handle_calibration(sample, events)
      return events

    self._handle_button(sample)
    self._accumulate_sample(sample)

    if self.last_rms_ms is None:
      self.last_rms_ms = sample.time_ms
    if self.last_second_ms is None:
      self.last_second_ms = sample.time_ms
    if self.last_minute_ms is None:
      self.last_minute_ms = sample.time_ms
    if self.start_time_ms is None:
      self.start_time_ms = sample.time_ms

    while sample.time_ms - self.last_rms_ms >= self.rms_window_ms:
      self.last_rms_ms += self.rms_window_ms
      self._finalize_rms_window()

    outputs_ready = False
    while sample.time_ms - self.last_second_ms >= self.second_window_ms:
      self.last_second_ms += self.second_window_ms
      outputs_ready = True

    while sample.time_ms - self.last_minute_ms >= self.minute_window_ms:
      self.last_minute_ms += self.minute_window_ms
      for ch in self.channels.values():
        ch.events_min = 0

    if outputs_ready:
      import logging
      logger = logging.getLogger("sleep-backend")
      logger.info("Output ready: generating processed sample at time_ms=%d", sample.time_ms)
      processed = self._build_processed_sample(sample.time_ms)
      logger.info("Built processed sample: time_s=%.1f", processed.time_s)
      events.append({"type": "data", "payload": processed})

    return events

  def _handle_calibration(self, sample: RawSample, events: List[Dict]):
    if not self._calibration_announced:
      events.append({"type": "status", "message": "calibrating"})
      self._calibration_announced = True
      import logging
      logger = logging.getLogger("sleep-backend")
      logger.info("Starting calibration, need %d samples", self.calibration_sample_target)

    if self.calibration_started_ms is None:
      self.calibration_started_ms = sample.time_ms

    for name in self.CHANNELS:
      self.calibration_samples[name].append(sample.piezo[name])
    self.sound_calibration.append(sample.sound)

    # Check sample count instead of device time
    if len(self.sound_calibration) < self.calibration_sample_target:
      import logging
      logger = logging.getLogger("sleep-backend")
      if len(self.sound_calibration) % 10 == 0:
        logger.info("Calibration progress: %d/%d", len(self.sound_calibration), self.calibration_sample_target)
      return

    for name in self.CHANNELS:
      values = self.calibration_samples[name]
      if not values:
        continue
      baseline = sum(values) / len(values)
      acc2 = 0.0
      for raw in values:
        adj = raw - baseline
        if adj < 0:
          adj = 0
        amp = adj * self.gain_vib
        acc2 += amp * amp
      idle_rms = math.sqrt(acc2 / len(values)) if values else 0.0
      state = self.channels[name]
      state.baseline = baseline
      state.noise_ema = idle_rms * state.config.norm_gain
      state.vib_thresh = max(
        (state.noise_ema + self.vib_margin_add) * state.config.region_mult,
        state.config.abs_move_thresh,
      )
      state.prev_rms_raw = idle_rms
      state.moving = False
      state.acc2 = 0.0
      state.count = 0
      state.rms = 0.0
      state.rms_filtered = 0.0  # Start from zero to avoid decay artifacts
      state.events_sec = 0
      state.events_min = 0
      state.activity_level = 0

    if self.sound_calibration:
      acc2 = 0.0
      for raw in self.sound_calibration:
        acc2 += float(raw) * float(raw)
      idle_sound = math.sqrt(acc2 / len(self.sound_calibration)) if self.sound_calibration else 0.0
      self.sound_thresh = idle_sound + 10.0

    self.calibrating = False
    self._calibration_announced = False
    # Initialize timers after calibration
    self.last_rms_ms = sample.time_ms
    self.last_second_ms = sample.time_ms
    self.last_minute_ms = sample.time_ms
    self.start_time_ms = sample.time_ms
    events.append({"type": "status", "message": "calibrated"})

  def _handle_button(self, sample: RawSample):
    down = sample.button == 0
    if down and not self.button_down:
      self.button_down = True
      self.button_press_start = sample.time_ms
      return

    if not down and self.button_down:
      self.button_down = False
      if self.button_press_start is None:
        return
      held = sample.time_ms - self.button_press_start
      if held >= self.button_long_press_ms:
        self.reset_calibration()
      elif held > self.button_debounce_ms:
        self.monitoring = not self.monitoring

  def _accumulate_sample(self, sample: RawSample):
    for name in self.CHANNELS:
      state = self.channels[name]
      adj = sample.piezo[name] - state.baseline
      if adj < 0:
        adj = 0
      amp = adj * self.gain_vib
      state.acc2 += amp * amp
      state.count += 1

    self.snd_acc2 += float(sample.sound) * float(sample.sound)
    self.snd_cnt += 1

    self.light_sum += sample.light
    self.light_cnt += 1

    self.temp_sum += sample.temp_raw
    self.temp_cnt += 1

  def _finalize_rms_window(self):
    for name in self.CHANNELS:
      state = self.channels[name]
      r_raw = math.sqrt(state.acc2 / state.count) if state.count > 0 else 0.0
      if state.prev_rms_raw > 0 and r_raw > self.spike_factor * state.prev_rms_raw:
        r_raw = state.prev_rms_raw

      r_norm = r_raw * state.config.norm_gain
      
      # Apply EMA smoothing to reduce noise
      if state.rms_filtered == 0.0:
        state.rms_filtered = r_norm
      else:
        state.rms_filtered = (self.filter_alpha * r_norm) + ((1.0 - self.filter_alpha) * state.rms_filtered)

      thresh_dyn = (state.noise_ema + self.vib_margin_add) * state.config.region_mult
      thresh = max(thresh_dyn, state.config.abs_move_thresh)
      
      # Use filtered signal for detection
      value = state.rms_filtered

      rising = (not state.moving) and (value > thresh + self.hyst_add)
      falling = state.moving and (value < thresh - self.hyst_add)

      if not state.moving:
        state.noise_ema = (1.0 - self.noise_alpha) * state.noise_ema + self.noise_alpha * value

      state.vib_thresh = (state.noise_ema + self.vib_margin_add) * state.config.region_mult
      if state.vib_thresh < state.config.abs_move_thresh:
        state.vib_thresh = state.config.abs_move_thresh

      if rising:
        state.moving = True
        if self.monitoring:
          state.events_sec += 1
      elif falling:
        state.moving = False

      # Classify activity level based on ratio to adaptive baseline
      # Use noise_ema (adaptive idle level) as reference instead of absolute values
      baseline_ref = max(state.noise_ema, 0.1)
      ratio = value / baseline_ref
      
      if ratio >= 2.5:  # 2.5x above baseline
        state.activity_level = 2  # Needs attention
      elif ratio >= 1.5:  # 1.5x above baseline
        state.activity_level = 1  # Slight movement
      else:
        state.activity_level = 0  # Idle

      state.rms = state.rms_filtered
      state.prev_rms_raw = r_raw
      state.acc2 = 0.0
      state.count = 0

  def _build_processed_sample(self, current_ms: int) -> ProcessedSample:
    if self.start_time_ms is None:
      self.start_time_ms = current_ms
    light_avg = int(self.light_sum / self.light_cnt) if self.light_cnt else 0
    temp_avg = self.temp_sum / self.temp_cnt if self.temp_cnt else 0.0
    sound_rms = math.sqrt(self.snd_acc2 / self.snd_cnt) if self.snd_cnt else 0.0

    self.light_sum = 0
    self.light_cnt = 0
    self.temp_sum = 0
    self.temp_cnt = 0
    self.snd_acc2 = 0.0
    self.snd_cnt = 0

    events_sec = {name: state.events_sec for name, state in self.channels.items()}
    if self.monitoring:
      for state in self.channels.values():
        state.events_min += state.events_sec
    events_min = {name: state.events_min for name, state in self.channels.items()}

    total_min = sum(events_min.values())
    
    # Realistic sleep score based on multiple factors
    score = 100.0
    
    # Factor 1: Movement events (discrete threshold crossings)
    # Penalize 3 points per event in current minute
    score -= total_min * 3.0
    
    # Factor 2: Overall movement intensity (RMS levels)
    # Sum current RMS values weighted by region importance
    current_rms = self.channels["head"].rms * 2.0  # head movement more significant
    current_rms += self.channels["body"].rms * 1.5
    current_rms += self.channels["leg"].rms * 1.0
    # Penalize if total weighted RMS > 2.0 (indicates restlessness even without events)
    if current_rms > 2.0:
      score -= (current_rms - 2.0) * 5.0
    
    # Factor 3: Activity level penalties
    for name, state in self.channels.items():
      if state.activity_level == 2:  # Needs attention
        score -= 8.0
      elif state.activity_level == 1:  # Slight movement
        score -= 3.0
    
    # Factor 4: Environmental disturbances
    if sound_rms > 150:  # noisy environment
      score -= (sound_rms - 150) * 0.05
    
    sleep_score = max(0.0, min(100.0, score))

    for state in self.channels.values():
      state.events_sec = 0

    time_s = (current_ms - self.start_time_ms) / 1000.0

    rms = {name: state.rms for name, state in self.channels.items()}
    moving = {name: state.moving for name, state in self.channels.items()}
    activity = {name: state.activity_level for name, state in self.channels.items()}
    
    # Overall status based on max activity level
    status_level = max(activity.values()) if activity else 0
    status_labels = {0: "Idle", 1: "Slight movement", 2: "Needs attention"}
    status_label = status_labels.get(status_level, "Idle")

    temp_c = self._adc_to_temp_c(temp_avg)

    return ProcessedSample(
      time_s=time_s,
      rms=rms,
      moving=moving,
      events_sec=events_sec,
      events_min=events_min,
      total_events_min=total_min,
      sleep_score=sleep_score,
      sound_rms=sound_rms,
      light_avg=light_avg,
      temp_c=temp_c,
      monitoring=self.monitoring,
      activity=activity,
      status_level=status_level,
      status_label=status_label,
    )
