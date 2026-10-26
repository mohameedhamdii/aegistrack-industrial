"""
Database models and Pydantic validation schemas for Asset Tracker.
"""

from datetime import datetime, timezone
from typing import List, Optional, Any, Dict
from pydantic import BaseModel, Field, ConfigDict
from sqlalchemy import (
    Column,
    Integer,
    Float,
    String,
    DateTime,
    Boolean,
    ForeignKey,
    Text,
    Index,
)
from sqlalchemy.orm import declarative_base, relationship

Base = declarative_base()


# ──────────────────────────── SQLAlchemy ORM Models ────────────────────────────


class TrackerModel(Base):
    __tablename__ = "trackers"

    id = Column(String(64), primary_key=True, index=True)
    name = Column(String(128), nullable=False)
    type = Column(String(32), default="container")  # 'container' | 'livestock'
    status = Column(String(32), default="active")   # 'active' | 'warning' | 'stale' | 'offline'
    created_at = Column(DateTime, default=lambda: datetime.now(timezone.utc))
    last_seen = Column(DateTime, nullable=True)

    # Relationships
    positions = relationship("PositionModel", back_populates="tracker", cascade="all, delete-orphan", order_by="desc(PositionModel.timestamp)")
    geofences = relationship("GeofenceModel", back_populates="tracker", cascade="all, delete-orphan")
    alerts = relationship("AlertModel", back_populates="tracker", cascade="all, delete-orphan", order_by="desc(AlertModel.timestamp)")
    downlinks = relationship("DownlinkCommandModel", back_populates="tracker", cascade="all, delete-orphan")


class PositionModel(Base):
    __tablename__ = "positions"

    id = Column(Integer, primary_key=True, autoincrement=True)
    tracker_id = Column(String(64), ForeignKey("trackers.id", ondelete="CASCADE"), nullable=False, index=True)
    timestamp = Column(DateTime, nullable=False, index=True)
    latitude = Column(Float, nullable=False)
    longitude = Column(Float, nullable=False)
    altitude = Column(Float, nullable=True, default=0.0)      # meters
    speed = Column(Float, nullable=True, default=0.0)         # m/s
    heading = Column(Float, nullable=True, default=0.0)       # degrees (0-360)
    battery_mv = Column(Integer, nullable=False, default=3600)# mV
    temperature = Column(Float, nullable=True, default=20.0)  # Celsius
    fix_type = Column(Integer, default=3)                     # 0=none, 2=2D, 3=3D
    hdop = Column(Float, default=1.0)
    geofence_event = Column(Integer, default=0)               # 0=none, 1=enter, 2=exit, 3=dwell
    geofence_zone = Column(Integer, default=0)
    anomaly_flags = Column(Integer, default=0)                # bit 0=kalman residual, bit 1=overspeed
    eta_minutes = Column(Integer, default=0)
    sequence = Column(Integer, default=0)

    tracker = relationship("TrackerModel", back_populates="positions")

    __table_args__ = (
        Index("idx_positions_tracker_ts", "tracker_id", "timestamp"),
        Index("idx_positions_lat_lon", "latitude", "longitude"),
    )


class GeofenceModel(Base):
    __tablename__ = "geofences"

    id = Column(Integer, primary_key=True, autoincrement=True)
    tracker_id = Column(String(64), ForeignKey("trackers.id", ondelete="CASCADE"), nullable=False, index=True)
    zone_id = Column(Integer, nullable=False)
    name = Column(String(128), default="Zone")
    type = Column(String(16), nullable=False)                 # 'circular' | 'polygonal'
    center_lat = Column(Float, nullable=True)
    center_lon = Column(Float, nullable=True)
    radius_m = Column(Float, nullable=True, default=500.0)
    vertices_json = Column(Text, nullable=True)               # JSON string of [[lat, lon], ...]
    dwell_time_s = Column(Integer, default=1800)
    trigger_immediate = Column(Boolean, default=True)
    created_at = Column(DateTime, default=lambda: datetime.now(timezone.utc))

    tracker = relationship("TrackerModel", back_populates="geofences")


class AlertModel(Base):
    __tablename__ = "alerts"

    id = Column(Integer, primary_key=True, autoincrement=True)
    tracker_id = Column(String(64), ForeignKey("trackers.id", ondelete="CASCADE"), nullable=False, index=True)
    timestamp = Column(DateTime, default=lambda: datetime.now(timezone.utc), index=True)
    alert_type = Column(String(64), nullable=False)           # 'GEOFENCE_ENTER', 'GEOFENCE_EXIT', 'KALMAN_ANOMALY', etc.
    severity = Column(String(16), default="info")             # 'info', 'warning', 'critical'
    message = Column(String(256), nullable=False)
    acknowledged = Column(Boolean, default=False)

    tracker = relationship("TrackerModel", back_populates="alerts")


class DownlinkCommandModel(Base):
    __tablename__ = "downlink_commands"

    id = Column(Integer, primary_key=True, autoincrement=True)
    tracker_id = Column(String(64), ForeignKey("trackers.id", ondelete="CASCADE"), nullable=False, index=True)
    cmd = Column(String(32), nullable=False)                  # 'set_interval', 'request_fix', 'set_geofence', 'ota_url'
    payload = Column(Text, nullable=True)                     # JSON string
    status = Column(String(16), default="pending")            # 'pending', 'sent', 'acknowledged', 'failed'
    created_at = Column(DateTime, default=lambda: datetime.now(timezone.utc))
    delivered_at = Column(DateTime, nullable=True)

    tracker = relationship("TrackerModel", back_populates="downlinks")


# ──────────────────────────── Pydantic Schemas ────────────────────────────


class TrackerBase(BaseModel):
    id: str
    name: str
    type: str = "container"


class TrackerCreate(TrackerBase):
    pass


class PositionOut(BaseModel):
    id: int
    tracker_id: str
    timestamp: datetime
    latitude: float
    longitude: float
    altitude: Optional[float] = 0.0
    speed: Optional[float] = 0.0
    heading: Optional[float] = 0.0
    battery_mv: int
    temperature: Optional[float] = 20.0
    fix_type: int = 3
    hdop: float = 1.0
    geofence_event: int = 0
    geofence_zone: int = 0
    anomaly_flags: int = 0
    eta_minutes: int = 0
    sequence: int = 0
    model_config = ConfigDict(from_attributes=True)


class TrackerOut(TrackerBase):
    status: str
    created_at: datetime
    last_seen: Optional[datetime] = None
    latest_position: Optional[PositionOut] = None
    model_config = ConfigDict(from_attributes=True)


class GeofenceCreate(BaseModel):
    zone_id: int
    name: str = "Zone"
    type: str = "circular"  # 'circular' | 'polygonal'
    center_lat: Optional[float] = None
    center_lon: Optional[float] = None
    radius_m: Optional[float] = 500.0
    vertices: Optional[List[List[float]]] = None  # [[lat, lon], ...]
    dwell_time_s: int = 1800
    trigger_immediate: bool = True


class GeofenceOut(BaseModel):
    id: int
    tracker_id: str
    zone_id: int
    name: str
    type: str
    center_lat: Optional[float] = None
    center_lon: Optional[float] = None
    radius_m: Optional[float] = None
    vertices: Optional[List[List[float]]] = None
    dwell_time_s: int
    trigger_immediate: bool
    created_at: datetime
    model_config = ConfigDict(from_attributes=True)


class AlertOut(BaseModel):
    id: int
    tracker_id: str
    timestamp: datetime
    alert_type: str
    severity: str
    message: str
    acknowledged: bool
    model_config = ConfigDict(from_attributes=True)


class DownlinkCommandCreate(BaseModel):
    cmd: str
    payload: Optional[Dict[str, Any]] = None


class DownlinkCommandOut(BaseModel):
    id: int
    tracker_id: str
    cmd: str
    payload: Optional[str] = None
    status: str
    created_at: datetime
    delivered_at: Optional[datetime] = None
    model_config = ConfigDict(from_attributes=True)


class TelemetryJsonIngest(BaseModel):
    """Optional JSON ingestion structure mirroring CBOR telemetry."""
    id: str
    ts: Optional[int] = None
    lat: float
    lon: float
    alt: Optional[float] = 0.0
    spd: Optional[float] = 0.0
    hdg: Optional[float] = 0.0
    bat: Optional[int] = 3600
    temp: Optional[float] = 22.0
    fix: Optional[int] = 3
    hdop: Optional[float] = 1.0
    geo_evt: Optional[int] = 0
    geo_zone: Optional[int] = 0
    anomaly: Optional[int] = 0
    eta_min: Optional[int] = 0
    seq: Optional[int] = 0
