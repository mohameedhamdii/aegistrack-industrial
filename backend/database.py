"""
Database connection, session management, and initial data seeding.
Supports both PostgreSQL (+ PostGIS) and SQLite for instant local runs.
"""

import os
import json
from datetime import datetime, timezone, timedelta
from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker, Session
from .models import Base, TrackerModel, PositionModel, GeofenceModel, AlertModel

DATABASE_URL = os.getenv("DATABASE_URL", "sqlite:///./asset_tracker.db")

# SQLite needs check_same_thread=False
connect_args = {}
if DATABASE_URL.startswith("sqlite"):
    connect_args = {"check_same_thread": False}

engine = create_engine(
    DATABASE_URL,
    connect_args=connect_args,
    pool_pre_ping=True,
)

SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)


def get_db():
    """FastAPI dependency for database session."""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def init_db():
    """Create all tables and seed sample assets if empty."""
    Base.metadata.create_all(bind=engine)

    with SessionLocal() as db:
        tracker_count = db.query(TrackerModel).count()
        if tracker_count == 0:
            seed_initial_data(db)


def seed_initial_data(db: Session):
    """Seed industrial shipping containers and livestock trackers with historical breadcrumbs."""
    now = datetime.now(timezone.utc)

    # 1. Industrial Refrigerated Container (Rotterdam Harbor -> Antwerp Logistics Route)
    cont1 = TrackerModel(
        id="TRACKER-CONT-001",
        name="Maersk Reefer #4192 (Pharma Logistics)",
        type="container",
        status="active",
        created_at=now - timedelta(days=14),
        last_seen=now - timedelta(minutes=4),
    )
    db.add(cont1)

    # Rotterdam Port Geofence (Polygon)
    rotterdam_poly = [
        [51.9600, 4.1000],
        [51.9700, 4.1400],
        [51.9550, 4.1800],
        [51.9350, 4.1300],
        [51.9450, 4.0900],
    ]
    geo1 = GeofenceModel(
        tracker_id="TRACKER-CONT-001",
        zone_id=1,
        name="Rotterdam Maasvlakte Terminal",
        type="polygonal",
        vertices_json=json.dumps(rotterdam_poly),
        dwell_time_s=3600,
        trigger_immediate=True,
    )
    db.add(geo1)

    # Antwerp Destination Geofence (Circular)
    geo2 = GeofenceModel(
        tracker_id="TRACKER-CONT-001",
        zone_id=2,
        name="Port of Antwerp Distribution Hub",
        type="circular",
        center_lat=51.2750,
        center_lon=4.3850,
        radius_m=850.0,
        dwell_time_s=1800,
        trigger_immediate=True,
    )
    db.add(geo2)

    # Historical breadcrumbs along A16 / E19 freight highway from Rotterdam southwards
    route_points = [
        (51.9520, 4.1250, 0.0, 3640, 4.5, 0),         # Docked Rotterdam
        (51.9350, 4.2200, 18.2, 3635, 4.8, 0),        # Departing port
        (51.8900, 4.3800, 22.4, 3630, 5.1, 0),        # Highway entry
        (51.7800, 4.6200, 24.1, 3625, 5.4, 0),        # Southbound transit
        (51.6500, 4.7100, 23.8, 3620, 5.2, 0),        # Breda interchange
        (51.5200, 4.6900, 24.5, 3615, 5.5, 0),        # Crossing border
        (51.3900, 4.5800, 22.9, 3610, 5.3, 0),        # Approaching Antwerp
        (51.2850, 4.3980, 8.4, 3605, 5.1, 1),         # Near Antwerp hub (Geofence Alert!)
    ]

    for i, (lat, lon, spd, bat, temp, geo_evt) in enumerate(route_points):
        ts = now - timedelta(hours=(len(route_points) - 1 - i) * 2)
        pos = PositionModel(
            tracker_id="TRACKER-CONT-001",
            timestamp=ts,
            latitude=lat,
            longitude=lon,
            altitude=12.0 + i * 2,
            speed=spd,
            heading=195.0,
            battery_mv=bat,
            temperature=temp,
            fix_type=3,
            hdop=0.9,
            geofence_event=geo_evt,
            geofence_zone=2 if geo_evt > 0 else 0,
            anomaly_flags=0,
            eta_minutes=25 if i < len(route_points) - 1 else 5,
            sequence=100 + i,
        )
        db.add(pos)

    # 2. Industrial Heavy Machinery / Generator (North Sea Wind Farm Offshore Support)
    cont2 = TrackerModel(
        id="TRACKER-GEN-104",
        name="Atlas Copco 500kVA Mobile GenSet",
        type="container",
        status="active",
        created_at=now - timedelta(days=30),
        last_seen=now - timedelta(minutes=12),
    )
    db.add(cont2)

    gen_positions = [
        (53.4500, 5.8200, 0.0, 3590, 14.2),
        (53.4510, 5.8210, 0.0, 3585, 14.5),
        (53.4505, 5.8205, 0.0, 3580, 14.1),
    ]
    for i, (lat, lon, spd, bat, temp) in enumerate(gen_positions):
        ts = now - timedelta(hours=(3 - i) * 6)
        db.add(PositionModel(
            tracker_id="TRACKER-GEN-104",
            timestamp=ts,
            latitude=lat,
            longitude=lon,
            altitude=3.5,
            speed=spd,
            heading=0.0,
            battery_mv=bat,
            temperature=temp,
            fix_type=3,
            hdop=1.1,
            geofence_event=0,
            geofence_zone=0,
            anomaly_flags=0,
            eta_minutes=0,
            sequence=200 + i,
        ))

    # 3. Free-Range Cattle Herd Alpha (Livestock in Wyoming Prairie Range)
    bull = TrackerModel(
        id="TRACKER-BULL-042",
        name="Black Angus Herd #42 (Grand Teton Range)",
        type="livestock",
        status="warning",
        created_at=now - timedelta(days=60),
        last_seen=now - timedelta(minutes=1),
    )
    db.add(bull)

    # Ranch Geofence (Circular)
    geo_ranch = GeofenceModel(
        tracker_id="TRACKER-BULL-042",
        zone_id=1,
        name="Diamond Cross Prairie Perimeter",
        type="circular",
        center_lat=43.7900,
        center_lon=-110.6800,
        radius_m=3500.0,
        dwell_time_s=7200,
        trigger_immediate=True,
    )
    db.add(geo_ranch)

    bull_points = [
        (43.7850, -110.6750, 0.4, 3520, 16.0, 0),
        (43.7920, -110.6650, 0.8, 3515, 17.5, 0),
        (43.8050, -110.6500, 1.2, 3510, 16.8, 0),
        (43.8250, -110.6300, 2.1, 3500, 15.2, 2),  # Exited ranch boundary!
    ]
    for i, (lat, lon, spd, bat, temp, geo_evt) in enumerate(bull_points):
        ts = now - timedelta(hours=(len(bull_points) - 1 - i) * 3)
        db.add(PositionModel(
            tracker_id="TRACKER-BULL-042",
            timestamp=ts,
            latitude=lat,
            longitude=lon,
            altitude=2040.0,
            speed=spd,
            heading=45.0,
            battery_mv=bat,
            temperature=temp,
            fix_type=3,
            hdop=1.3,
            geofence_event=geo_evt,
            geofence_zone=1 if geo_evt > 0 else 0,
            anomaly_flags=1 if geo_evt == 2 else 0,
            eta_minutes=0,
            sequence=300 + i,
        ))

    # Add realistic Alert logs
    db.add(AlertModel(
        tracker_id="TRACKER-BULL-042",
        timestamp=now - timedelta(hours=1),
        alert_type="GEOFENCE_EXIT",
        severity="critical",
        message="Asset exited Diamond Cross Prairie Perimeter boundary (Heading 45 deg, Speed 2.1 m/s)",
        acknowledged=False,
    ))

    db.add(AlertModel(
        tracker_id="TRACKER-CONT-001",
        timestamp=now - timedelta(hours=2),
        alert_type="GEOFENCE_ENTER",
        severity="info",
        message="Asset arrived within Port of Antwerp Distribution Hub perimeter (Radius 850m)",
        acknowledged=True,
    ))

    db.commit()
