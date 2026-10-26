# Plan 06 — Multi-Year Asset Tracker for Containers/Livestock

> **One-liner:** A battery-powered tracker that locates containers or livestock outside cellular coverage for years on a single charge, using Kalman-filtered GPS fusion and extreme power engineering.

---

## 1. Project Overview

### 1.1 Goal
Build an ultra-low-power asset tracker that acquires GPS fixes, fuses them with a Kalman filter for smooth trajectory estimation and ETA prediction, transmits location data over NB-IoT or satellite, implements geofencing with on-device anomaly detection, and operates for 2+ years on a single battery — all through aggressive power gating and duty cycling.

### 1.2 Domain
Industrial & Asset Tracking

### 1.3 Portfolio Value
Combines classical estimation theory (Kalman filtering) with extreme power engineering. The multi-year battery life requirement forces careful analysis of every microamp, making this a standout project for embedded systems roles.

---

## 2. Hardware Bill of Materials

| Component | Specific Part / Model | Qty | Purpose |
|---|---|---|---|
| MCU | STM32L4R5ZI (ultra-low-power, 640 KB SRAM) | 1 | Main processor — runs KF, geofence, comms |
| GNSS module | u-blox MAX-M10S (or SAM-M10Q) | 1 | GPS/GNSS positioning (< 5 mA acquisition) |
| Cellular modem | u-blox SARA-R410M (NB-IoT / LTE-M) | 1 | Cellular data transmission |
| Satellite modem (optional) | u-blox NEO-D9S + L-band antenna OR RockBLOCK 9603 | 1 | Satellite fallback for areas with no cellular |
| Accelerometer | LIS2DW12 (ultra-low-power 3-axis, 1 µA) | 1 | Motion detection for wake-on-movement |
| Battery | Tadiran TL-5930 3.6 V D-cell lithium thionyl chloride (19 Ah) | 1 | Primary cell — 2+ year runtime |
| Antenna (GNSS) | Passive patch antenna | 1 | GPS reception |
| Antenna (cellular) | Flexible PCB antenna for NB-IoT bands | 1 | Cellular transmission |
| SIM card | IoT SIM (e.g., 1NCE — 10-year plan, 500 MB) | 1 | Cellular connectivity |
| Enclosure | IP67 ABS enclosure with bolt-mount flanges | 1 | Weatherproof, attach to container or animal collar |
| Temperature sensor | Internal MCU die temperature or TMP117 (optional) | 1 | Environmental monitoring |
| PCB | Custom 2-layer PCB (designed in KiCad) | 1 | Integrates all components |

### 2.1 Power Budget Analysis

| Component | Active Current | Duration | Duty | Avg Current |
|---|---|---|---|---|
| STM32L4 — Run mode (64 MHz) | 3.5 mA | 200 ms | per fix | — |
| STM32L4 — Standby (RTC only) | 0.6 µA | 99.5% of time | — | 0.6 µA |
| GNSS — Cold start acquisition | 12 mA | 30 s | 1× every 6 hrs | 16.7 µA |
| GNSS — Hot start acquisition | 5 mA | 3 s | most fixes | — |
| GNSS — Power off | 0 µA | 99.9% of time | — | 0 µA |
| NB-IoT modem — TX | 230 mA | 5 s | 1× every 6 hrs | 80 µA |
| NB-IoT modem — PSM (power saving mode) | 3 µA | 99.9% of time | — | 3 µA |
| Accelerometer — low-power mode | 1 µA | 100% | always-on | 1 µA |
| **Total average** | — | — | — | **~21 µA** |
| **Battery life (19 Ah)** | — | — | — | **~103 years theoretical** |

*Real-world factors (self-discharge, cold temperature, ageing) reduce this to a practical 2–5 years.*

---

## 3. Software Stack & Dependencies

| Layer | Technology | Notes |
|---|---|---|
| Firmware | STM32 HAL + bare-metal (no RTOS needed) | C, ultra-lean |
| GNSS driver | u-blox UBX protocol parser | Binary protocol for efficiency |
| Kalman filter | Custom linear KF | Position + velocity state estimation |
| Geofencing | Point-in-polygon (ray-casting algorithm) | Multiple configurable geofence zones |
| NB-IoT driver | AT command interface over UART | SARA-R410M AT command set |
| Cloud backend | Node.js or Python (FastAPI) | Receives tracker data, stores, serves API |
| Database | PostgreSQL + PostGIS (geospatial) | Location history with geospatial queries |
| Dashboard | Leaflet.js or Mapbox GL JS | Live map with tracker positions, geofences, history trails |
| OTA firmware update | MQTT-based delta update | Field firmware updates |

---

## 4. System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    TRACKER DEVICE                               │
│                                                                 │
│  ┌───────────────┐                                              │
│  │ LIS2DW12      │ ← Always-on, 1 µA                           │
│  │ Accelerometer │──── INT1 ─────────────┐                      │
│  └───────────────┘                       │ wake-on-motion       │
│                                          ▼                      │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                   STM32L4R5 MCU                          │   │
│  │                                                          │   │
│  │  ┌─────────┐   ┌──────────┐   ┌─────────────────┐       │   │
│  │  │  GNSS   │──▶│  Kalman  │──▶│ Geofence Check  │       │   │
│  │  │  Driver │   │  Filter  │   │ + Anomaly Detect │       │   │
│  │  └─────────┘   └──────────┘   └────────┬────────┘       │   │
│  │                                         │                │   │
│  │                                         ▼                │   │
│  │                               ┌──────────────────┐       │   │
│  │                               │  Packet Assembler │      │   │
│  │                               │  (CBOR encoded)   │      │   │
│  │                               └────────┬─────────┘       │   │
│  │                                        │                 │   │
│  │                                        ▼                 │   │
│  │                               ┌──────────────────┐       │   │
│  │                               │  NB-IoT / Sat TX │       │   │
│  │                               └──────────────────┘       │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                          ▲                      │
│  ┌──────────────────┐                    │                      │
│  │ Lithium Thionyl  │────────────────────┘                      │
│  │ Chloride Battery │                                           │
│  │ (19 Ah, 3.6 V)   │                                           │
│  └──────────────────┘                                           │
└─────────────────────────────────────────────────────────────────┘
                    │ NB-IoT / Satellite
                    ▼
┌─────────────────────────────────────────────────────────────────┐
│                    CLOUD BACKEND                                │
│  ┌─────────────┐  ┌────────────────┐  ┌───────────────────┐    │
│  │  API Server │──│ PostgreSQL     │──│ Map Dashboard     │    │
│  │  (FastAPI)  │  │ + PostGIS      │  │ (Leaflet/Mapbox)  │    │
│  └─────────────┘  └────────────────┘  └───────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

---

## 5. Detailed Module Breakdown

### 5.1 Module A — Power Management State Machine (`power.c`)

**Responsibility:** Control all power states and peripheral power gating. This is the most critical module — it determines whether the device achieves multi-year battery life.

**Implementation details:**
1. **Operating modes:**

| Mode | Trigger | What's Active | Current |
|---|---|---|---|
| STANDBY | Default between fixes | RTC + accelerometer (via EXTI) | 0.6 µA + 1 µA |
| MOTION_DETECT | Accelerometer INT1 fires | MCU wakes briefly to confirm motion | ~200 µA for 10 ms |
| GNSS_ACQUIRE | Scheduled fix or motion confirmed | MCU + GNSS module powered on | 5–12 mA |
| TRANSMIT | After fix acquired | MCU + NB-IoT modem | 230 mA peak |
| STANDBY_MOVING | Object is in motion | More frequent fixes (every 15 min vs. 6 hr) | Higher avg current |

2. **State transitions:**
   - **Stationary:** Device in STANDBY. RTC wakes every 6 hours for a scheduled fix. Accelerometer INT1 also triggers wake.
   - **Motion detected:** Accelerometer INT fires → MCU wakes → confirms sustained motion (> 3 s of accelerometer readings above threshold) → enters STANDBY_MOVING mode → fixes every 15 minutes.
   - **Motion stopped:** No accelerometer activity for 5 minutes → return to STANDBY, 6-hour fix interval.
3. **Peripheral power gating:**
   - GNSS module: power controlled by a GPIO-switched load switch (TPS22918). Power ON only during fix acquisition. **Critical: use hot-start where possible** (backup GNSS ephemeris in MCU flash).
   - NB-IoT modem: use Power Saving Mode (PSM) with T3324 = 0 (immediate PSM entry). Power ON only for TX, then return to PSM.
   - Accelerometer: always on at 1 µA. Use hardware interrupt for wake — do NOT poll.

**Files to create:**
- `firmware/src/power/power_manager.h / .c`
- `firmware/src/power/sleep.h / .c` (STM32 Standby/Stop mode wrappers)
- `firmware/src/power/peripheral_switch.h / .c` (load switch GPIO control)

---

### 5.2 Module B — GNSS Driver (`gnss/`)

**Responsibility:** Acquire GPS fixes efficiently with minimal power consumption.

**Implementation details:**
1. **UBX protocol:** Communicate with u-blox GNSS over UART using binary UBX protocol (more efficient than NMEA).
2. **Fix acquisition strategy:**
   - **Hot start:** If last fix was < 4 hours ago and ephemeris is still valid, hot start takes ~3 s.
   - **Warm start:** If ephemeris expired but almanac is valid, ~20 s.
   - **Cold start:** First fix after power-on, ~30 s. Minimise by saving ephemeris data.
3. **Ephemeris caching (AssistNow Offline):**
   - Download a 2-week ephemeris file from u-blox AssistNow Offline service during each NB-IoT connection.
   - Store in MCU flash (32 KB).
   - Inject into GNSS module on power-up for fast TTFF (time to first fix).
4. **Accuracy:** Target < 3 m CEP. Use GNSS + SBAS (satellite-based augmentation).
5. **Fix timeout:** If no fix within 60 s, abort and transmit a "no fix" packet. Record failure for diagnostics.

**Files to create:**
- `firmware/src/gnss/gnss_driver.h / .c` (UBX protocol, UART communication)
- `firmware/src/gnss/ubx_parser.h / .c` (UBX message parsing)
- `firmware/src/gnss/ephemeris_cache.h / .c` (flash storage of AssistNow data)

---

### 5.3 Module C — Kalman Filter (`kalman/`)

**Responsibility:** Fuse noisy GPS fixes into a smooth position/velocity estimate and predict future positions (ETA).

**Implementation details:**
1. **State vector:** `x = [lat, lon, v_lat, v_lon]` (position + velocity in 2D).
2. **Process model (constant velocity):**
   ```
   x_k = F * x_{k-1} + w
   F = [1  0  dt  0 ]
       [0  1  0   dt]
       [0  0  1   0 ]
       [0  0  0   1 ]
   ```
   Process noise `Q`: tuned for expected asset dynamics (slow-moving container vs. livestock).
3. **Measurement model:**
   ```
   z_k = H * x_k + v
   H = [1  0  0  0]
       [0  1  0  0]
   ```
   Measurement noise `R`: derived from GPS accuracy (HDOP × base sigma ≈ 3–10 m).
4. **Predict step:** Run between fixes using the time delta since last fix. Provides interpolated position estimates.
5. **ETA prediction:**
   - Given a destination geofence centroid and current velocity estimate, compute ETA:
     `ETA = distance_to_destination / speed`.
   - Update ETA in each transmitted packet.
6. **Anomaly detection:**
   - If the Kalman innovation (measurement residual) exceeds 5× expected, flag as potential GPS spoofing or multipath error.
   - If speed exceeds physically plausible maximum (e.g., 200 km/h for a container), flag as anomaly.

**Files to create:**
- `firmware/src/kalman/kalman_filter.h / .c` (generic 4-state linear KF)
- `firmware/src/kalman/asset_tracker_kf.h / .c` (asset-tracking-specific wrapper)
- `firmware/src/kalman/eta_predictor.h / .c`

---

### 5.4 Module D — Geofencing Engine (`geofence/`)

**Responsibility:** Define geographic boundary zones and detect when the asset enters or exits them.

**Implementation details:**
1. **Geofence storage:** Up to 8 geofence zones stored in MCU flash.
2. **Geofence types:**
   - **Circular:** centre (lat, lon) + radius in metres. Simple distance check.
   - **Polygonal:** up to 8 vertices. Ray-casting point-in-polygon algorithm.
3. **Event generation:**
   - `GEOFENCE_ENTER`: Asset position transitions from outside to inside a zone.
   - `GEOFENCE_EXIT`: Asset position transitions from inside to outside a zone.
   - `GEOFENCE_DWELL`: Asset inside a zone for > configurable duration (e.g., 30 min).
4. **On geofence event:**
   - Set event flag in next transmission packet.
   - If configured, trigger an immediate transmission (override scheduled interval).
5. **Geofence configuration:** Received from the backend via downlink (NB-IoT or satellite). Stored in MCU flash. Updated OTA.

**Configuration structure:**
```c
typedef struct {
    uint8_t  zone_id;
    uint8_t  type;           // 0 = circular, 1 = polygonal
    uint8_t  num_vertices;   // 0 for circular
    int32_t  center_lat;     // × 1e7
    int32_t  center_lon;     // × 1e7
    uint32_t radius_m;       // for circular
    int32_t  vertices[8][2]; // lat/lon pairs for polygonal
    uint32_t dwell_time_s;
    uint8_t  trigger_immediate_tx; // 0 or 1
} geofence_zone_t;
```

**Files to create:**
- `firmware/src/geofence/geofence_engine.h / .c`
- `firmware/src/geofence/point_in_polygon.h / .c`
- `firmware/src/geofence/geofence_storage.h / .c` (flash read/write)

---

### 5.5 Module E — Communication (`comms/`)

**Responsibility:** Transmit data packets over NB-IoT (primary) or satellite (fallback).

**Implementation details:**
1. **NB-IoT driver (SARA-R410M):**
   - AT command interface over UART (115200 baud).
   - **Connection flow:**
     1. Power ON modem (GPIO load switch).
     2. Wait for `+CEREG: 1` or `+CEREG: 5` (registered or roaming).
     3. Open UDP socket to backend server.
     4. Send CBOR-encoded data packet (< 200 bytes).
     5. Receive any downlink commands (geofence updates, config changes).
     6. Close socket, enter PSM.
   - **PSM configuration:** `AT+CPSMS=1,,,10100101,00100001` — active timer = 2 s, periodic TAU = 6 hours.
   - **Fallback:** If NB-IoT registration fails after 60 s, abort. Retry next scheduled fix. After 3 consecutive failures, switch to satellite (if available).
2. **Packet structure (CBOR-encoded, ~60 bytes):**
```
{
  "id": "TRACKER-001",
  "ts": 1694000000,
  "lat": 51.5074,
  "lon": -0.1278,
  "alt": 12.5,
  "spd": 3.2,
  "hdg": 145.0,
  "bat": 3580,          // mV
  "temp": 22,           // °C
  "fix_type": 3,        // 3D fix
  "hdop": 1.2,
  "geo_evt": 0,         // 0=none, 1=enter, 2=exit, 3=dwell
  "geo_zone": 0,
  "anomaly": 0,
  "eta_min": 120,       // ETA in minutes to next geofence
  "seq": 4521           // sequence number
}
```
3. **Downlink commands (JSON, received from server):**
   - `{"cmd": "set_interval", "value": 900}` — change fix interval to 15 min.
   - `{"cmd": "set_geofence", "zones": [...]}` — update geofence configuration.
   - `{"cmd": "request_fix"}` — trigger immediate fix and transmission.
   - `{"cmd": "ota_url", "url": "..."}` — initiate firmware update.

**Files to create:**
- `firmware/src/comms/nbiot_driver.h / .c` (AT command interface)
- `firmware/src/comms/packet.h / .c` (CBOR encoding)
- `firmware/src/comms/downlink.h / .c` (command parsing)
- `firmware/src/comms/satellite.h / .c` (optional Iridium SBD)

---

### 5.6 Module F — Cloud Backend & Dashboard (`backend/` + `dashboard/`)

**Responsibility:** Receive tracker data, store with geospatial indexing, and provide a live map dashboard.

**Implementation details:**

#### Backend (FastAPI + PostgreSQL/PostGIS):
1. **API endpoints:**
   - `POST /api/v1/ingest` — receives CBOR packet from tracker, decodes, stores in DB.
   - `GET /api/v1/trackers` — list all trackers with latest position.
   - `GET /api/v1/trackers/{id}/history?from=&to=` — location history.
   - `GET /api/v1/trackers/{id}/geofences` — geofence configurations.
   - `PUT /api/v1/trackers/{id}/geofences` — update geofences (queued for next downlink).
   - `GET /api/v1/trackers/{id}/alerts` — geofence events and anomalies.
   - WebSocket `/ws/live` — push real-time position updates to dashboard.
2. **Database schema:**
```sql
CREATE TABLE trackers (
    id VARCHAR(32) PRIMARY KEY,
    name VARCHAR(128),
    type VARCHAR(32),  -- 'container' or 'livestock'
    created_at TIMESTAMP
);

CREATE TABLE positions (
    id SERIAL PRIMARY KEY,
    tracker_id VARCHAR(32) REFERENCES trackers(id),
    timestamp TIMESTAMP NOT NULL,
    location GEOGRAPHY(POINT, 4326) NOT NULL,
    altitude FLOAT,
    speed FLOAT,
    heading FLOAT,
    battery_mv INTEGER,
    temperature FLOAT,
    fix_type INTEGER,
    hdop FLOAT,
    geofence_event INTEGER,
    geofence_zone INTEGER,
    anomaly_flags INTEGER,
    eta_minutes INTEGER,
    sequence INTEGER
);

CREATE INDEX idx_positions_tracker_ts ON positions(tracker_id, timestamp DESC);
CREATE INDEX idx_positions_location ON positions USING GIST(location);

CREATE TABLE geofences (
    id SERIAL PRIMARY KEY,
    tracker_id VARCHAR(32) REFERENCES trackers(id),
    zone_id INTEGER,
    type VARCHAR(16),  -- 'circular' or 'polygon'
    center GEOGRAPHY(POINT, 4326),
    radius_m FLOAT,
    polygon GEOGRAPHY(POLYGON, 4326),
    dwell_time_s INTEGER,
    trigger_immediate BOOLEAN
);
```

#### Dashboard (Leaflet.js / Mapbox):
1. **Map view:** Full-screen map with tracker markers at their last known positions.
2. **Tracker marker:** Custom icon (container or animal). Colour-coded:
   - Green: normal, recent fix (< 12 hours old).
   - Yellow: stale fix (12–48 hours old).
   - Red: anomaly or geofence alert.
3. **Click tracker → detail panel:**
   - Battery level, last fix time, speed, ETA.
   - Trail line showing last 7 days of positions.
   - Geofence zones drawn on map (circles or polygons).
4. **Geofence editor:** Click-to-draw circles or polygons on the map, save to backend.
5. **Alert panel:** Sidebar listing geofence events and anomalies, newest first.

**Files to create:**
- `backend/main.py`, `backend/api/routes.py`, `backend/models.py`, `backend/database.py`
- `backend/requirements.txt`, `backend/Dockerfile`
- `dashboard/index.html`, `dashboard/js/map.js`, `dashboard/js/tracker.js`, `dashboard/js/geofence-editor.js`
- `dashboard/css/style.css`

---

## 6. Directory Structure

```
asset-tracker/
├── README.md
├── firmware/
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.c
│   │   ├── power/
│   │   │   ├── power_manager.h / .c
│   │   │   ├── sleep.h / .c
│   │   │   └── peripheral_switch.h / .c
│   │   ├── gnss/
│   │   │   ├── gnss_driver.h / .c
│   │   │   ├── ubx_parser.h / .c
│   │   │   └── ephemeris_cache.h / .c
│   │   ├── kalman/
│   │   │   ├── kalman_filter.h / .c
│   │   │   ├── asset_tracker_kf.h / .c
│   │   │   └── eta_predictor.h / .c
│   │   ├── geofence/
│   │   │   ├── geofence_engine.h / .c
│   │   │   ├── point_in_polygon.h / .c
│   │   │   └── geofence_storage.h / .c
│   │   ├── comms/
│   │   │   ├── nbiot_driver.h / .c
│   │   │   ├── packet.h / .c
│   │   │   ├── downlink.h / .c
│   │   │   └── satellite.h / .c
│   │   └── sensors/
│   │       ├── accelerometer.h / .c
│   │       └── temperature.h / .c
│   ├── config/
│   │   ├── pin_map.h
│   │   ├── power_config.h
│   │   └── comms_config.h
│   └── tests/
│       ├── test_kalman.c
│       ├── test_geofence.c
│       ├── test_ubx_parser.c
│       └── test_packet.c
├── backend/
│   ├── main.py
│   ├── api/routes.py
│   ├── models.py
│   ├── database.py
│   ├── requirements.txt
│   └── Dockerfile
├── dashboard/
│   ├── index.html
│   ├── js/
│   │   ├── map.js
│   │   ├── tracker.js
│   │   └── geofence-editor.js
│   └── css/style.css
├── hardware/
│   ├── kicad/
│   │   ├── asset-tracker.kicad_pro
│   │   ├── asset-tracker.kicad_sch
│   │   └── asset-tracker.kicad_pcb
│   ├── bom.csv
│   └── enclosure_stl/
│       └── tracker_case.stl
├── docker-compose.yaml
└── docs/
    ├── power_budget_analysis.md
    ├── deployment_guide.md
    └── pcb_assembly_guide.md
```

---

## 7. Testing Strategy

| Test | Type | What it validates |
|---|---|---|
| `test_kalman.c` | Unit | KF converges on synthetic GPS data; velocity estimates correct |
| `test_geofence.c` | Unit | Point-in-polygon correct for 10 test polygons; enter/exit events fire correctly |
| `test_ubx_parser.c` | Unit | UBX messages parsed correctly for NAV-PVT, NAV-STATUS |
| `test_packet.c` | Unit | CBOR encoding/decoding produces correct values |
| Power measurement | Hardware | Measure current with µA-resolution meter (e.g., Nordic PPK2) in all states; verify matches budget |
| NB-IoT integration | Integration | Tracker transmits to backend; data appears in DB and dashboard |
| 7-day field test | System | Tracker on a moving vehicle for 7 days; verify trail on dashboard, geofence events, battery consumption |

---

## 8. Stretch Goals

- **OTA firmware update:** Download delta firmware update over NB-IoT, apply on-device.
- **Dead reckoning:** Fuse accelerometer data during GPS-denied periods for continuous tracking.
- **Animal behaviour classification:** Use accelerometer patterns to classify livestock activity (grazing, walking, resting).
- **Fleet management dashboard:** Multi-tracker view with route optimisation.
- **eSIM support:** Switch cellular operators dynamically based on coverage.

---

## 9. Risks & Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| NB-IoT coverage gaps in rural areas | No data transmission | Add satellite backhaul; queue packets for later transmission |
| GPS cold start takes too long | Excessive power drain | Use AssistNow Offline ephemeris caching; abort after 60 s |
| Battery self-discharge over years | Shorter life than calculated | Use lithium thionyl chloride (LTC) cells with < 1%/year self-discharge |
| Livestock remove/damage tracker | Data loss | Ruggedise enclosure; use collar mount with safety breakaway |
| KF divergence from prolonged stationary periods | Bad position estimates | Reset KF velocity to zero after sustained no-motion from accelerometer |

---

## 10. Estimated Timeline

| Phase | Duration | Deliverable |
|---|---|---|
| PCB design (KiCad) & component sourcing | 5 days | PCB sent for fabrication |
| PCB assembly & bring-up | 3 days | All peripherals communicating |
| GNSS driver + UBX parser | 3 days | GPS fixes acquired |
| Kalman filter implementation | 3 days | Smooth trajectory estimation working |
| Geofence engine | 2 days | Enter/exit events correct |
| NB-IoT communication | 3 days | Data transmitted to cloud |
| Power management & sleep states | 3 days | Current draw matches budget |
| Backend + dashboard | 4 days | Map dashboard live |
| Field testing & power measurement | 3 days | 7-day field test passed |
| **Total** | **~29 days** | — |
