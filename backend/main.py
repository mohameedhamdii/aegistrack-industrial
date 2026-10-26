"""
FastAPI application entry point for Asset Tracker IoT Cloud Backend.
Includes REST API, WebSocket live streaming, UDP server for direct NB-IoT datagrams,
and static file serving for the dashboard.
"""

import os
import asyncio
import logging
from contextlib import asynccontextmanager
from pathlib import Path
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, RedirectResponse

from .database import init_db, SessionLocal
from .api.routes import router as api_router, ws_manager
from .cbor_decoder import decode_cbor_telemetry
from .models import PositionModel, TrackerModel, AlertModel, DownlinkCommandModel
from datetime import datetime, timezone
import json

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("asset_tracker")

UDP_PORT = int(os.getenv("UDP_PORT", "5683"))


class UdpServerProtocol(asyncio.DatagramProtocol):
    """Async UDP server protocol to ingest raw CBOR packets sent directly by SARA-R410M modems."""

    def __init__(self):
        super().__init__()
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport
        logger.info(f"NB-IoT UDP server listening on port {UDP_PORT}")

    def datagram_received(self, data: bytes, addr):
        logger.info(f"Received {len(data)} bytes UDP datagram from {addr}")
        asyncio.create_task(self.process_datagram(data, addr))

    async def process_datagram(self, data: bytes, addr):
        try:
            telemetry = decode_cbor_telemetry(data)
            tracker_id = telemetry["tracker_id"]
            logger.info(f"Decoded telemetry from {tracker_id}: lat={telemetry['latitude']}, lon={telemetry['longitude']}, bat={telemetry['battery_mv']}mV")

            with SessionLocal() as db:
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

                tracker.last_seen = telemetry["timestamp"]

                pos = PositionModel(
                    tracker_id=tracker_id,
                    timestamp=telemetry["timestamp"],
                    latitude=telemetry["latitude"],
                    longitude=telemetry["longitude"],
                    altitude=telemetry.get("altitude", 0.0),
                    speed=telemetry.get("speed", 0.0),
                    heading=telemetry.get("heading", 0.0),
                    battery_mv=telemetry.get("battery_mv", 3600),
                    temperature=telemetry.get("temperature", 20.0),
                    fix_type=telemetry.get("fix_type", 3),
                    hdop=telemetry.get("hdop", 1.0),
                    geofence_event=telemetry.get("geofence_event", 0),
                    geofence_zone=telemetry.get("geofence_zone", 0),
                    anomaly_flags=telemetry.get("anomaly_flags", 0),
                    eta_minutes=telemetry.get("eta_minutes", 0),
                    sequence=telemetry.get("sequence", 0),
                )
                db.add(pos)

                # Check for pending downlinks
                pending_cmd = db.query(DownlinkCommandModel).filter(
                    DownlinkCommandModel.tracker_id == tracker_id,
                    DownlinkCommandModel.status == "pending",
                ).order_by(DownlinkCommandModel.created_at.asc()).first()

                if pending_cmd and self.transport:
                    pending_cmd.status = "sent"
                    pending_cmd.delivered_at = datetime.now(timezone.utc)
                    payload_data = json.loads(pending_cmd.payload) if pending_cmd.payload else {}
                    dl_msg = json.dumps({"cmd": pending_cmd.cmd, **payload_data}).encode("utf-8")
                    self.transport.sendto(dl_msg, addr)
                    logger.info(f"Dispatched downlink UDP to {addr}: {pending_cmd.cmd}")

                db.commit()

                broadcast_data = {
                    "type": "POSITION_UPDATE",
                    "tracker": {
                        "id": tracker.id,
                        "name": tracker.name,
                        "type": tracker.type,
                        "status": tracker.status,
                        "last_seen": tracker.last_seen.isoformat() if tracker.last_seen else None,
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
                        "timestamp": pos.timestamp.isoformat() if pos.timestamp else None,
                    },
                }

            # Push live update to connected dashboards
            await ws_manager.broadcast(broadcast_data)

        except Exception as e:
            logger.error(f"Error processing UDP packet from {addr}: {e}")


@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup: Initialize Database
    logger.info("Initializing Asset Tracker Database...")
    init_db()

    # Startup: Async UDP listener for direct hardware cellular telemetry
    loop = asyncio.get_running_loop()
    transport = None
    try:
        transport, _ = await loop.create_datagram_endpoint(
            lambda: UdpServerProtocol(),
            local_addr=("0.0.0.0", UDP_PORT),
        )
        logger.info(f"UDP datagram listener bound to 0.0.0.0:{UDP_PORT}")
    except Exception as e:
        logger.warning(f"Could not bind UDP listener on port {UDP_PORT}: {e}")

    yield

    # Shutdown
    if transport:
        transport.close()
    logger.info("Asset Tracker server shutdown complete.")


app = FastAPI(
    title="Asset Tracker Industrial IoT Cloud Server",
    description="Multi-Year Asset Tracking Cloud Backend (FastAPI, PostGIS/SQLite, CBOR, WebSockets)",
    version="1.0.0",
    lifespan=lifespan,
)

# CORS Middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Mount REST API
app.include_router(api_router, prefix="/api/v1")


# WebSocket live stream
@app.websocket("/ws/live")
async def websocket_live_endpoint(websocket: WebSocket):
    await ws_manager.connect(websocket)
    logger.info("Dashboard connected to live WebSocket stream")
    try:
        while True:
            # Keepalive / client message receive
            data = await websocket.receive_text()
            if data == "ping":
                await websocket.send_text("pong")
    except WebSocketDisconnect:
        ws_manager.disconnect(websocket)
        logger.info("Dashboard disconnected from WebSocket stream")


# Mount Dashboard static assets
dashboard_dir = Path(__file__).resolve().parent.parent / "dashboard"
if dashboard_dir.exists():
    css_dir = dashboard_dir / "css"
    js_dir = dashboard_dir / "js"
    if css_dir.exists():
        app.mount("/css", StaticFiles(directory=str(css_dir)), name="css")
    if js_dir.exists():
        app.mount("/js", StaticFiles(directory=str(js_dir)), name="js")
    app.mount("/static", StaticFiles(directory=str(dashboard_dir)), name="static")

    @app.get("/")
    async def serve_dashboard():
        index_file = dashboard_dir / "index.html"
        if index_file.exists():
            return FileResponse(str(index_file))
        return {"status": "Asset Tracker Backend Running", "api_docs": "/docs"}
