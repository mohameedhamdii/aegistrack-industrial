"""
CBOR packet decoding module for Asset Tracker telemetry frames.
Decodes binary RFC 7049 payloads transmitted over NB-IoT UDP or HTTP.
"""

from typing import Dict, Any
from datetime import datetime, timezone
import cbor2


def decode_cbor_telemetry(raw_bytes: bytes) -> Dict[str, Any]:
    """
    Decode raw CBOR binary packet into normalized telemetry dictionary.
    Converts fixed-point MCU integers to SI/metric units (degrees, meters, m/s).
    """
    if not raw_bytes:
        raise ValueError("Empty telemetry packet buffer")

    data = cbor2.loads(raw_bytes)

    if not isinstance(data, dict):
        raise ValueError(f"Expected CBOR Map, got {type(data)}")

    tracker_id = str(data.get("id", "UNKNOWN-TRACKER"))

    # Timestamp conversion (epoch seconds -> UTC datetime)
    ts_raw = data.get("ts")
    if ts_raw:
        timestamp = datetime.fromtimestamp(ts_raw, tz=timezone.utc)
    else:
        timestamp = datetime.now(timezone.utc)

    # Latitude & Longitude (fixed-point x 1e7 -> float degrees)
    lat_raw = data.get("lat", 0)
    lon_raw = data.get("lon", 0)
    latitude = round(float(lat_raw) / 1e7, 7)
    longitude = round(float(lon_raw) / 1e7, 7)

    # Altitude in mm -> meters
    alt_raw = data.get("alt", 0)
    altitude = round(float(alt_raw) / 1000.0, 2)

    # Speed in mm/s -> m/s
    spd_raw = data.get("spd", 0)
    speed = round(float(spd_raw) / 1000.0, 2)

    # Heading in degrees x 1e5 -> float degrees
    hdg_raw = data.get("hdg", 0)
    heading = round(float(hdg_raw) / 1e5, 1)

    # Battery in mV
    battery_mv = int(data.get("bat", 3600))

    # Temperature in deg C
    temperature = float(data.get("temp", 20.0))

    # Fix & HDOP
    fix_type = int(data.get("fix", 3))
    hdop_raw = data.get("hdop", 100)
    hdop = round(float(hdop_raw) / 100.0, 2)

    # Geofencing
    geofence_event = int(data.get("geo_evt", 0))
    geofence_zone = int(data.get("geo_zone", 0))

    # Anomaly & Sequence
    anomaly_flags = int(data.get("anomaly", 0))
    sequence = int(data.get("seq", 0))

    return {
        "tracker_id": tracker_id,
        "timestamp": timestamp,
        "latitude": latitude,
        "longitude": longitude,
        "altitude": altitude,
        "speed": speed,
        "heading": heading,
        "battery_mv": battery_mv,
        "temperature": temperature,
        "fix_type": fix_type,
        "hdop": hdop,
        "geofence_event": geofence_event,
        "geofence_zone": geofence_zone,
        "anomaly_flags": anomaly_flags,
        "sequence": sequence,
    }
