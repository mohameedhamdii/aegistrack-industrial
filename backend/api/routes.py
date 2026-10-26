"""
API routes for Asset Tracker IoT backend.
Handles binary CBOR ingestion, REST queries, geofence updates, downlink queuing, and WebSocket broadcasting.
"""

import json
from datetime import datetime, timezone
from typing import List, Optional, Set
from fastapi import APIRouter, Depends, HTTPException, Request, Response, WebSocket, WebSocketDisconnect
from sqlalchemy.orm import Session
from sqlalchemy import desc

from ..database import get_db
from ..models import (
    TrackerModel,
    PositionModel,
    GeofenceModel,
    AlertModel,
    DownlinkCommandModel,
    TrackerCreate,
    TrackerOut,
    PositionOut,
    GeofenceCreate,
    GeofenceOut,
    AlertOut,
    DownlinkCommandCreate,
    DownlinkCommandOut,
    TelemetryJsonIngest,
)
from ..cbor_decoder import decode_cbor_telemetry

router = APIRouter()

# ──────────────────────────── WebSocket Manager ────────────────────────────

class ConnectionManager:
    def __init__(self):
        self.active_connections: Set[WebSocket] = set()

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.add(websocket)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.discard(websocket)

    async def broadcast(self, message: dict):
        dead_connections = set()
        for connection in self.active_connections:
            try:
                await connection.send_json(message)
            except Exception:
                dead_connections.add(connection)
        for dead in dead_connections:
            self.active_connections.discard(dead)

ws_manager = ConnectionManager()


# ──────────────────────────── Ingestion Endpoint ────────────────────────────

@router.post("/ingest")
async def ingest_telemetry(request: Request, db: Session = Depends(get_db)):
    """
    Ingest telemetry packet from tracker device.
    Supports binary CBOR (RFC 7049) and JSON.
    Returns any pending downlink commands for the tracker.
    """
    content_type = request.headers.get("content-type", "").lower()

    if "application/cbor" in content_type or "application/octet-stream" in content_type:
        raw_body = await request.body()
        try:
            data = decode_cbor_telemetry(raw_body)
        except Exception as e:
            raise HTTPException(status_code=400, detail=f"Invalid CBOR packet: {str(e)}")
    else:
        # Fallback to JSON
        try:
            json_body = await request.json()
            payload = TelemetryJsonIngest(**json_body)
            data = {
                "tracker_id": payload.id,
                "timestamp": datetime.fromtimestamp(payload.ts, tz=timezone.utc) if payload.ts else datetime.now(timezone.utc),
                "latitude": payload.lat,
                "longitude": payload.lon,
                "altitude": payload.alt,
                "speed": payload.spd,
                "heading": payload.hdg,
                "battery_mv": payload.bat,
                "temperature": payload.temp,
                "fix_type": payload.fix,
                "hdop": payload.hdop,
                "geofence_event": payload.geo_evt,
                "geofence_zone": payload.geo_zone,
                "anomaly_flags": payload.anomaly,
                "eta_minutes": payload.eta_min,
                "sequence": payload.seq,
            }
        except Exception as e:
            raise HTTPException(status_code=400, detail=f"Invalid JSON payload: {str(e)}")

    tracker_id = data["tracker_id"]

    # Verify or register tracker
    tracker = db.query(TrackerModel).filter(TrackerModel.id == tracker_id).first()
    if not tracker:
        tracker = TrackerModel(
            id=tracker_id,
            name=f"Asset {tracker_id}",
            type="container" if "CONT" in tracker_id else "livestock",
            status="active",
            created_at=datetime.now(timezone.utc),
        )
        db.add(tracker)

    tracker.last_seen = data["timestamp"]

    # Record position
    pos = PositionModel(
        tracker_id=tracker_id,
        timestamp=data["timestamp"],
        latitude=data["latitude"],
        longitude=data["longitude"],
        altitude=data.get("altitude", 0.0),
        speed=data.get("speed", 0.0),
        heading=data.get("heading", 0.0),
        battery_mv=data.get("battery_mv", 3600),
        temperature=data.get("temperature", 20.0),
        fix_type=data.get("fix_type", 3),
        hdop=data.get("hdop", 1.0),
        geofence_event=data.get("geofence_event", 0),
        geofence_zone=data.get("geofence_zone", 0),
        anomaly_flags=data.get("anomaly_flags", 0),
        eta_minutes=data.get("eta_minutes", 0),
        sequence=data.get("sequence", 0),
    )
    db.add(pos)

    # Automatic alert evaluation
    alert_created = None
    if pos.geofence_event == 1:
        alert_created = AlertModel(
            tracker_id=tracker_id,
            timestamp=pos.timestamp,
            alert_type="GEOFENCE_ENTER",
            severity="info",
            message=f"Entered geofence zone #{pos.geofence_zone}",
        )
        db.add(alert_created)
    elif pos.geofence_event == 2:
        alert_created = AlertModel(
            tracker_id=tracker_id,
            timestamp=pos.timestamp,
            alert_type="GEOFENCE_EXIT",
            severity="warning",
            message=f"Exited geofence zone #{pos.geofence_zone}",
        )
        db.add(alert_created)
        tracker.status = "warning"
    elif pos.geofence_event == 3:
        alert_created = AlertModel(
            tracker_id=tracker_id,
            timestamp=pos.timestamp,
            alert_type="GEOFENCE_DWELL",
            severity="info",
            message=f"Dwelling inside geofence zone #{pos.geofence_zone}",
        )
        db.add(alert_created)

    if pos.anomaly_flags & 0x01:
        alert_created = AlertModel(
            tracker_id=tracker_id,
            timestamp=pos.timestamp,
            alert_type="KALMAN_ANOMALY",
            severity="critical",
            message="Kalman filter innovation threshold exceeded (suspected GPS multipath or spoofing)",
        )
        db.add(alert_created)
        tracker.status = "warning"

    if pos.battery_mv < 3300:
        alert_created = AlertModel(
            tracker_id=tracker_id,
            timestamp=pos.timestamp,
            alert_type="BATTERY_LOW",
            severity="warning",
            message=f"Battery voltage low: {pos.battery_mv} mV",
        )
        db.add(alert_created)

    # Check for pending downlinks to send in response
    pending_cmd = db.query(DownlinkCommandModel).filter(
        DownlinkCommandModel.tracker_id == tracker_id,
        DownlinkCommandModel.status == "pending",
    ).order_by(DownlinkCommandModel.created_at.asc()).first()

    downlink_response = None
    if pending_cmd:
        pending_cmd.status = "sent"
        pending_cmd.delivered_at = datetime.now(timezone.utc)
        payload_data = json.loads(pending_cmd.payload) if pending_cmd.payload else {}
        downlink_response = {"cmd": pending_cmd.cmd, **payload_data}

    db.commit()

    # Broadcast to dashboard via WebSocket
    broadcast_data = {
        "type": "POSITION_UPDATE",
        "tracker": {
            "id": tracker.id,
            "name": tracker.name,
            "type": tracker.type,
            "status": tracker.status,
            "last_seen": tracker.last_seen.isoformat(),
        },
        "position": {
            "latitude": pos.latitude,
            "longitude": pos.longitude,
            "altitude": pos.altitude,
            "speed": pos.speed,
            "heading": pos.heading,
            "battery_mv": pos.battery_mv,
            "temperature": pos.temperature,
            "fix_type": pos.fix_type,
            "hdop": pos.hdop,
            "geofence_event": pos.geofence_event,
            "geofence_zone": pos.geofence_zone,
            "anomaly_flags": pos.anomaly_flags,
            "eta_minutes": pos.eta_minutes,
            "sequence": pos.sequence,
            "timestamp": pos.timestamp.isoformat(),
        },
    }
    if alert_created:
        broadcast_data["alert"] = {
            "id": alert_created.id,
            "tracker_id": tracker_id,
            "alert_type": alert_created.alert_type,
            "severity": alert_created.severity,
            "message": alert_created.message,
            "timestamp": alert_created.timestamp.isoformat(),
        }

    await ws_manager.broadcast(broadcast_data)

    return {
        "status": "success",
        "tracker_id": tracker_id,
        "sequence": pos.sequence,
        "downlink": downlink_response,
    }


# ──────────────────────────── Trackers API ────────────────────────────

@router.get("/trackers", response_model=List[TrackerOut])
def list_trackers(db: Session = Depends(get_db)):
    """List all registered trackers with their latest known position."""
    trackers = db.query(TrackerModel).all()
    out = []
    for t in trackers:
        latest = db.query(PositionModel).filter(
            PositionModel.tracker_id == t.id
        ).order_by(desc(PositionModel.timestamp)).first()

        t_dict = {
            "id": t.id,
            "name": t.name,
            "type": t.type,
            "status": t.status,
            "created_at": t.created_at,
            "last_seen": t.last_seen,
            "latest_position": latest,
        }
        out.append(t_dict)
    return out


@router.post("/trackers", response_model=TrackerOut)
def create_tracker(tracker_in: TrackerCreate, db: Session = Depends(get_db)):
    """Register a new tracker device."""
    existing = db.query(TrackerModel).filter(TrackerModel.id == tracker_in.id).first()
    if existing:
        raise HTTPException(status_code=400, detail="Tracker ID already registered")

    tracker = TrackerModel(
        id=tracker_in.id,
        name=tracker_in.name,
        type=tracker_in.type,
        status="active",
        created_at=datetime.now(timezone.utc),
    )
    db.add(tracker)
    db.commit()
    db.refresh(tracker)
    return tracker


@router.get("/trackers/{tracker_id}", response_model=TrackerOut)
def get_tracker(tracker_id: str, db: Session = Depends(get_db)):
    """Get single tracker profile."""
    tracker = db.query(TrackerModel).filter(TrackerModel.id == tracker_id).first()
    if not tracker:
        raise HTTPException(status_code=404, detail="Tracker not found")

    latest = db.query(PositionModel).filter(
        PositionModel.tracker_id == tracker.id
    ).order_by(desc(PositionModel.timestamp)).first()

    return {
        "id": tracker.id,
        "name": tracker.name,
        "type": tracker.type,
        "status": tracker.status,
        "created_at": tracker.created_at,
        "last_seen": tracker.last_seen,
        "latest_position": latest,
    }


@router.get("/trackers/{tracker_id}/history", response_model=List[PositionOut])
def get_tracker_history(
    tracker_id: str,
    limit: int = 200,
    db: Session = Depends(get_db)
):
    """Get historical trajectory breadcrumbs for a tracker."""
    positions = db.query(PositionModel).filter(
        PositionModel.tracker_id == tracker_id
    ).order_by(desc(PositionModel.timestamp)).limit(limit).all()

    # Return chronological order (oldest -> newest for polyline drawing)
    return list(reversed(positions))


# ──────────────────────────── Geofencing API ────────────────────────────

@router.get("/trackers/{tracker_id}/geofences", response_model=List[GeofenceOut])
def get_tracker_geofences(tracker_id: str, db: Session = Depends(get_db)):
    """Get all geofence zones configured for a tracker."""
    geofences = db.query(GeofenceModel).filter(GeofenceModel.tracker_id == tracker_id).all()
    results = []
    for g in geofences:
        vertices = json.loads(g.vertices_json) if g.vertices_json else None
        results.append({
            "id": g.id,
            "tracker_id": g.tracker_id,
            "zone_id": g.zone_id,
            "name": g.name,
            "type": g.type,
            "center_lat": g.center_lat,
            "center_lon": g.center_lon,
            "radius_m": g.radius_m,
            "vertices": vertices,
            "dwell_time_s": g.dwell_time_s,
            "trigger_immediate": g.trigger_immediate,
            "created_at": g.created_at,
        })
    return results


@router.put("/trackers/{tracker_id}/geofences", response_model=GeofenceOut)
def upsert_geofence(
    tracker_id: str,
    geo_in: GeofenceCreate,
    db: Session = Depends(get_db)
):
    """
    Create or update a geofence zone for a tracker.
    Automatically queues a downlink command for the hardware tracker.
    """
    tracker = db.query(TrackerModel).filter(TrackerModel.id == tracker_id).first()
    if not tracker:
        raise HTTPException(status_code=404, detail="Tracker not found")

    existing = db.query(GeofenceModel).filter(
        GeofenceModel.tracker_id == tracker_id,
        GeofenceModel.zone_id == geo_in.zone_id,
    ).first()

    vertices_json = json.dumps(geo_in.vertices) if geo_in.vertices else None

    if existing:
        existing.name = geo_in.name
        existing.type = geo_in.type
        existing.center_lat = geo_in.center_lat
        existing.center_lon = geo_in.center_lon
        existing.radius_m = geo_in.radius_m
        existing.vertices_json = vertices_json
        existing.dwell_time_s = geo_in.dwell_time_s
        existing.trigger_immediate = geo_in.trigger_immediate
        fence = existing
    else:
        fence = GeofenceModel(
            tracker_id=tracker_id,
            zone_id=geo_in.zone_id,
            name=geo_in.name,
            type=geo_in.type,
            center_lat=geo_in.center_lat,
            center_lon=geo_in.center_lon,
            radius_m=geo_in.radius_m,
            vertices_json=vertices_json,
            dwell_time_s=geo_in.dwell_time_s,
            trigger_immediate=geo_in.trigger_immediate,
        )
        db.add(fence)

    # Queue downlink command for device sync
    downlink_payload = {
        "zone_id": geo_in.zone_id,
        "type": 0 if geo_in.type == "circular" else 1,
        "center_lat": int((geo_in.center_lat or 0) * 1e7),
        "center_lon": int((geo_in.center_lon or 0) * 1e7),
        "radius_m": int(geo_in.radius_m or 0),
        "vertices": [[int(v[0] * 1e7), int(v[1] * 1e7)] for v in (geo_in.vertices or [])],
        "dwell_time_s": geo_in.dwell_time_s,
    }
    cmd = DownlinkCommandModel(
        tracker_id=tracker_id,
        cmd="set_geofence",
        payload=json.dumps(downlink_payload),
        status="pending",
    )
    db.add(cmd)

    db.commit()
    db.refresh(fence)

    return {
        "id": fence.id,
        "tracker_id": fence.tracker_id,
        "zone_id": fence.zone_id,
        "name": fence.name,
        "type": fence.type,
        "center_lat": fence.center_lat,
        "center_lon": fence.center_lon,
        "radius_m": fence.radius_m,
        "vertices": geo_in.vertices,
        "dwell_time_s": fence.dwell_time_s,
        "trigger_immediate": fence.trigger_immediate,
        "created_at": fence.created_at,
    }


@router.delete("/trackers/{tracker_id}/geofences/{zone_id}")
def delete_geofence(tracker_id: str, zone_id: int, db: Session = Depends(get_db)):
    """Delete a geofence zone."""
    fence = db.query(GeofenceModel).filter(
        GeofenceModel.tracker_id == tracker_id,
        GeofenceModel.zone_id == zone_id,
    ).first()
    if not fence:
        raise HTTPException(status_code=404, detail="Geofence not found")

    db.delete(fence)
    db.commit()
    return {"status": "deleted", "tracker_id": tracker_id, "zone_id": zone_id}


# ──────────────────────────── Alerts API ────────────────────────────

@router.get("/alerts", response_model=List[AlertOut])
def list_all_alerts(limit: int = 50, db: Session = Depends(get_db)):
    """List system-wide alerts, newest first."""
    return db.query(AlertModel).order_by(desc(AlertModel.timestamp)).limit(limit).all()


@router.get("/trackers/{tracker_id}/alerts", response_model=List[AlertOut])
def get_tracker_alerts(tracker_id: str, limit: int = 50, db: Session = Depends(get_db)):
    """List alerts for a specific tracker."""
    return db.query(AlertModel).filter(
        AlertModel.tracker_id == tracker_id
    ).order_by(desc(AlertModel.timestamp)).limit(limit).all()


@router.post("/alerts/{alert_id}/ack")
def acknowledge_alert(alert_id: int, db: Session = Depends(get_db)):
    """Acknowledge an active alert."""
    alert = db.query(AlertModel).filter(AlertModel.id == alert_id).first()
    if not alert:
        raise HTTPException(status_code=404, detail="Alert not found")
    alert.acknowledged = True
    db.commit()
    return {"status": "acknowledged", "alert_id": alert_id}


# ──────────────────────────── Downlink Commands API ────────────────────────────

@router.post("/trackers/{tracker_id}/command", response_model=DownlinkCommandOut)
def queue_downlink_command(
    tracker_id: str,
    cmd_in: DownlinkCommandCreate,
    db: Session = Depends(get_db)
):
    """Queue a downlink command (e.g. set_interval, request_fix, ota_url) for a tracker."""
    tracker = db.query(TrackerModel).filter(TrackerModel.id == tracker_id).first()
    if not tracker:
        raise HTTPException(status_code=404, detail="Tracker not found")

    payload_str = json.dumps(cmd_in.payload) if cmd_in.payload else None
    cmd = DownlinkCommandModel(
        tracker_id=tracker_id,
        cmd=cmd_in.cmd,
        payload=payload_str,
        status="pending",
    )
    db.add(cmd)
    db.commit()
    db.refresh(cmd)
    return cmd


@router.get("/trackers/{tracker_id}/downlink")
def get_pending_downlink(tracker_id: str, db: Session = Depends(get_db)):
    """Direct poll endpoint for hardware modems to retrieve queued downlink commands."""
    cmd = db.query(DownlinkCommandModel).filter(
        DownlinkCommandModel.tracker_id == tracker_id,
        DownlinkCommandModel.status == "pending",
    ).order_by(DownlinkCommandModel.created_at.asc()).first()

    if not cmd:
        return Response(status_code=204)

    cmd.status = "sent"
    cmd.delivered_at = datetime.now(timezone.utc)
    db.commit()

    payload_data = json.loads(cmd.payload) if cmd.payload else {}
    return {"cmd": cmd.cmd, **payload_data}
