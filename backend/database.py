from __future__ import annotations

import contextlib
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import List, Optional

from sqlalchemy import Boolean, Column, DateTime, Float, Integer, String, create_engine
from sqlalchemy.orm import Session, declarative_base, sessionmaker

from .pipeline import ProcessedSample

DB_PATH = Path(__file__).resolve().parent / "sleep_data.db"
DATABASE_URL = f"sqlite:///{DB_PATH}"  # auto-creates file if missing

engine = create_engine(
  DATABASE_URL,
  future=True,
  connect_args={"check_same_thread": False},  # allow threaded use via asyncio.to_thread
)
SessionLocal = sessionmaker(bind=engine, autoflush=False, expire_on_commit=False, class_=Session)
Base = declarative_base()


class ProcessedSampleModel(Base):
  __tablename__ = "processed_samples"

  id = Column(Integer, primary_key=True, index=True)
  recorded_at = Column(DateTime(timezone=True), nullable=False, index=True, default=lambda: datetime.now(timezone.utc))
  device_time_s = Column(Float, nullable=False)
  rms_head = Column(Float, nullable=False)
  rms_body = Column(Float, nullable=False)
  rms_leg = Column(Float, nullable=False)
  mov_head = Column(Boolean, nullable=False)
  mov_body = Column(Boolean, nullable=False)
  mov_leg = Column(Boolean, nullable=False)
  sec_head = Column(Integer, nullable=False)
  sec_body = Column(Integer, nullable=False)
  sec_leg = Column(Integer, nullable=False)
  min_head = Column(Integer, nullable=False)
  min_body = Column(Integer, nullable=False)
  min_leg = Column(Integer, nullable=False)
  total_events_min = Column(Integer, nullable=False)
  sleep_score = Column(Float, nullable=False)
  sound_rms = Column(Float, nullable=False)
  light_raw = Column(Integer, nullable=False)
  temp_c = Column(Float, nullable=False)
  monitoring = Column(Boolean, nullable=False, default=True)
  activity_head = Column(Integer, nullable=False, default=0)
  activity_body = Column(Integer, nullable=False, default=0)
  activity_leg = Column(Integer, nullable=False, default=0)
  status_level = Column(Integer, nullable=False, default=0)
  status_label = Column(String, nullable=False, default="Idle")

  def to_payload(self) -> dict:
    return {
      "time_s": self.device_time_s,
      "RMS_H": self.rms_head,
      "RMS_B": self.rms_body,
      "RMS_L": self.rms_leg,
      "MOV_H": 1 if self.mov_head else 0,
      "MOV_B": 1 if self.mov_body else 0,
      "MOV_L": 1 if self.mov_leg else 0,
      "secEv_H": self.sec_head,
      "secEv_B": self.sec_body,
      "secEv_L": self.sec_leg,
      "minute_events_H": self.min_head,
      "minute_events_B": self.min_body,
      "minute_events_L": self.min_leg,
      "total_events_min": self.total_events_min,
      "SleepScore": self.sleep_score,
      "SoundRMS": self.sound_rms,
      "LightRaw": self.light_raw,
      "TempC": self.temp_c,
      "monitoring": 1 if self.monitoring else 0,
      "activity_H": self.activity_head,
      "activity_B": self.activity_body,
      "activity_L": self.activity_leg,
      "status_level": self.status_level,
      "status_label": self.status_label,
      "recorded_at": self.recorded_at.isoformat(),
    }


def init_db() -> None:
  Base.metadata.create_all(bind=engine)


def save_processed_sample(sample: ProcessedSample) -> None:
  now = datetime.now(timezone.utc)
  model = ProcessedSampleModel(
    recorded_at=now,
    device_time_s=sample.time_s,
    rms_head=sample.rms["head"],
    rms_body=sample.rms["body"],
    rms_leg=sample.rms["leg"],
    mov_head=sample.moving["head"],
    mov_body=sample.moving["body"],
    mov_leg=sample.moving["leg"],
    sec_head=sample.events_sec["head"],
    sec_body=sample.events_sec["body"],
    sec_leg=sample.events_sec["leg"],
    min_head=sample.events_min["head"],
    min_body=sample.events_min["body"],
    min_leg=sample.events_min["leg"],
    total_events_min=sample.total_events_min,
    sleep_score=sample.sleep_score,
    sound_rms=sample.sound_rms,
    light_raw=sample.light_avg,
    temp_c=sample.temp_c,
    monitoring=bool(sample.monitoring),
    activity_head=sample.activity["head"],
    activity_body=sample.activity["body"],
    activity_leg=sample.activity["leg"],
    status_level=sample.status_level,
    status_label=sample.status_label,
  )
  with contextlib.closing(SessionLocal()) as session:
    session.add(model)
    session.commit()


def get_recent_samples(limit: int = 300) -> List[dict]:
  with contextlib.closing(SessionLocal()) as session:
    query = (
      session.query(ProcessedSampleModel)
      .order_by(ProcessedSampleModel.recorded_at.desc())
      .limit(limit)
    )
    rows = list(reversed(query.all()))
    return [row.to_payload() for row in rows]


def get_samples_since(minutes: int) -> List[dict]:
  cutoff = datetime.now(timezone.utc) - timedelta(minutes=minutes)
  with contextlib.closing(SessionLocal()) as session:
    rows = (
      session.query(ProcessedSampleModel)
      .filter(ProcessedSampleModel.recorded_at >= cutoff)
      .order_by(ProcessedSampleModel.recorded_at.asc())
      .all()
    )
    return [row.to_payload() for row in rows]


def get_latest_sample() -> Optional[dict]:
  with contextlib.closing(SessionLocal()) as session:
    row = (
      session.query(ProcessedSampleModel)
      .order_by(ProcessedSampleModel.recorded_at.desc())
      .first()
    )
    return row.to_payload() if row else None


def count_samples() -> int:
  with contextlib.closing(SessionLocal()) as session:
    return session.query(ProcessedSampleModel).count()


def clear_samples() -> None:
  with contextlib.closing(SessionLocal()) as session:
    session.query(ProcessedSampleModel).delete()
    session.commit()
