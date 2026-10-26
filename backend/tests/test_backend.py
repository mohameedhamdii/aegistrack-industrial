"""
End-to-End Unit & Integration tests for Asset Tracker Backend.
Validates CBOR decoding, REST endpoints, Geofence alerts, Downlink queuing, and Database persistence.
"""

import pytest
import cbor2
from datetime import datetime, timezone
from fastapi.testclient import TestClient

from backend.main import app
from backend.database import Base, engine, SessionLocal, init_db
from backend.models import TrackerModel, PositionModel, GeofenceModel, AlertModel
from backend.cbor_decoder import decode_cbor_telemetry

client = TestClient(app)


@pytest.fixture(autouse=True)
def setup_db():
    init_db()
    yield


def test_cbor_decoder_valid_packet():
    """Test decoding real binary CBOR packet from firmware."""
    packet_dict = {
        "id": "HW-UNIT-8821",
        "ts": 1761502000,
        "lat": 515074000,       # 51.5074 deg
        "lon": -1278000,        # -0.1278 deg
        "alt": 35000,           # 35.0 m
        "spd": 12000,           # 12.0 m/s
        "hdg": 18000000,        # 180.0 deg
        "bat": 3610,            # 3610 mV
        "temp": 19,             # 19 C
        "fix": 3,
        "hdop": 95,             # 0.95
        "geo_evt": 1,           # ENTER event
        "geo_zone": 1,
        "anomaly": 0,
        "seq": 42,
    }
    raw_cbor = cbor2.dumps(packet_dict)

    decoded = decode_cbor_telemetry(raw_cbor)
    assert decoded["tracker_id"] == "HW-UNIT-8821"
    assert abs(decoded["latitude"] - 51.5074) < 1e-4
    assert abs(decoded["longitude"] - (-0.1278)) < 1e-4
    assert decoded["altitude"] == 35.0
    assert decoded["speed"] == 12.0
    assert decoded["heading"] == 180.0
    assert decoded["battery_mv"] == 3610
    assert decoded["temperature"] == 19.0
    assert decoded["geofence_event"] == 1
    assert decoded["sequence"] == 42


def test_ingest_cbor_endpoint():
    """Test POST /api/v1/ingest with binary CBOR body."""
    packet_dict = {
        "id": "HW-TEST-INGEST",
        "ts": 1761503000,
        "lat": 523676000,   # 52.3676 (Amsterdam)
        "lon": 49041000,    # 4.9041
        "alt": 2000,
        "spd": 4500,
        "hdg": 27000000,
        "bat": 3580,
        "temp": 18,
        "fix": 3,
        "hdop": 80,
        "geo_evt": 0,
        "geo_zone": 0,
        "anomaly": 0,
        "seq": 101,
    }
    raw_cbor = cbor2.dumps(packet_dict)

    res = client.post(
        "/api/v1/ingest",
        content=raw_cbor,
        headers={"Content-Type": "application/cbor"},
    )
    assert res.status_code == 200
    res_data = res.json()
    assert res_data["status"] == "success"
    assert res_data["tracker_id"] == "HW-TEST-INGEST"
    assert res_data["sequence"] == 101


def test_list_trackers():
    """Test GET /api/v1/trackers returns seeded industrial assets."""
    res = client.get("/api/v1/trackers")
    assert res.status_code == 200
    trackers = res.json()
    assert len(trackers) >= 2

    ids = [t["id"] for t in trackers]
    assert "TRACKER-CONT-001" in ids


def test_tracker_history():
    """Test GET /api/v1/trackers/{id}/history returns trajectory."""
    res = client.get("/api/v1/trackers/TRACKER-CONT-001/history")
    assert res.status_code == 200
    history = res.json()
    assert len(history) > 0
    # Coordinates should be valid European lat/lon
    assert 50.0 < history[0]["latitude"] < 53.0
    assert 3.0 < history[0]["longitude"] < 6.0


def test_geofence_lifecycle_and_downlink():
    """Test creating a geofence and auto-queuing downlink command."""
    geo_payload = {
        "zone_id": 5,
        "name": "Custom Industrial Yard",
        "type": "circular",
        "center_lat": 51.5000,
        "center_lon": 4.2000,
        "radius_m": 1200.0,
        "dwell_time_s": 3600,
        "trigger_immediate": True,
    }

    res = client.put("/api/v1/trackers/TRACKER-CONT-001/geofences", json=geo_payload)
    assert res.status_code == 200
    geo = res.json()
    assert geo["zone_id"] == 5
    assert geo["radius_m"] == 1200.0

    # Device polls for downlink command
    res_dl = client.get("/api/v1/trackers/TRACKER-CONT-001/downlink")
    assert res_dl.status_code == 200
    dl_data = res_dl.json()
    assert dl_data["cmd"] == "set_geofence"
    assert dl_data["zone_id"] == 5
