# AegisTrack Industrial — Multi-Year Low-Power Asset Tracker

[![Firmware Build](https://img.shields.io/badge/firmware-STM32L4R5ZI-blue.svg)](file:///firmware)
[![Backend Tests](https://img.shields.io/badge/backend-pytest%20passing-brightgreen.svg)](file:///backend/tests)
[![Power Budget](https://img.shields.io/badge/average%20current-21%20%C2%B5A-orange.svg)](file:///docs/power_budget_analysis.md)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

> **Industrial-grade battery-powered asset tracker designed to locate intermodal shipping containers and free-range livestock across global supply chains and remote rangelands for 2 to 5+ years on a single Tadiran D-cell primary lithium battery.**

---

## 1. System Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│                        HARDWARE SENSOR NODE                            │
│                                                                        │
│  ┌───────────────────────┐         ┌────────────────────────────────┐  │
│  │ LIS2DW12 Accel (1 µA) │──INT1──▶│   STM32L4R5ZI MCU (Cortex-M4)  │  │
│  │ Wake-on-Motion EXTI   │         │                                │  │
│  └───────────────────────┘         │  • Linear Kalman Filter (4-st) │  │
│                                    │  • Ray-Casting Geofence Engine │  │
│  ┌───────────────────────┐         │  • CBOR Telemetry Encoder      │  │
│  │ u-blox MAX-M10S GNSS  │◀─PWR_EN─┤  • Deep Standby Power Manager  │  │
│  │ AssistNow Offline TTFF│         └───────────────┬────────────────┘  │
│  └───────────────────────┘                         │                   │
│                                                    ▼                   │
│  ┌───────────────────────┐         ┌────────────────────────────────┐  │
│  │ u-blox SARA-R410M     │◀─PWR_EN─┤ TPS22918 Load Switches         │  │
│  │ NB-IoT / LTE-M Modem  │         │ (Per-peripheral power gating)  │  │
│  └───────────┬───────────┘         └────────────────────────────────┘  │
│              │                                     ▲                   │
│              │                                     │                   │
│  ┌───────────┴───────────┐         ┌───────────────┴────────────────┐  │
│  │ Patch / Flex Antenna  │         │ Tadiran TL-5930 19 Ah D-Cell   │  │
│  └───────────────────────┘         └────────────────────────────────┘  │
└──────────────┼─────────────────────────────────────────────────────────┘
               │ NB-IoT UDP (Binary CBOR / RFC 7049)
               ▼
┌────────────────────────────────────────────────────────────────────────┐
│                       CLOUD BACKEND & DASHBOARD                        │
│                                                                        │
│  ┌───────────────────────┐         ┌────────────────────────────────┐  │
│  │ UDP Datagram Server   │────────▶│ FastAPI REST / WebSocket Core  │  │
│  │ Port 5683 (NB-IoT)    │         │                                │  │
│  └───────────────────────┘         └───────────────┬────────────────┘  │
│                                                    │                   │
│  ┌───────────────────────┐                         ▼                   │
│  │ PostgreSQL + PostGIS  │◀────────────────────────┤                   │
│  │ Geospatial Database   │                         ▼                   │
│  └───────────────────────┘         ┌────────────────────────────────┐  │
│                                    │ Industrial Leaflet Map UI      │  │
│                                    │ Real-time breadcrumbs & alerts │  │
│                                    └────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Core Engineering Highlights

### 2.1 Extreme Power Management (~21 µA Average Current)
- **True Peripheral Gating:** Hardware TPS22918 load switches completely cut leakage current to the GNSS and cellular modem rails to `0.0 µA` during sleep.
- **Microamp Duty Cycle:** STM32L4 Standby mode (0.6 µA) with RTC wakeup timer.
- **Always-on Motion Sensing:** ST LIS2DW12 operating at 1.0 µA with autonomous hardware interrupt on INT1 (wake-on-motion).
- **Ephemeris Caching:** AssistNow Offline ephemeris stored in flash enables hot-start GPS acquisition in `< 3 s` instead of a 30 s cold start.
- **Battery Life:** 19,000 mAh Tadiran TL-5930 primary cell yields **> 2 to 5 years** under real-world industrial temperature and transit profiles. See [Power Budget Analysis](file:///docs/power_budget_analysis.md).

### 2.2 Classical Estimation Theory (Kalman Filtering)
- **4-State Discrete Linear Kalman Filter:** State vector $x = [\text{lat}, \text{lon}, v_{\text{lat}}, v_{\text{lon}}]$.
- **Innovation Anomaly Detection:** Flags GPS multipath reflections, signal jamming, or spoofing by testing measurement residuals against covariance thresholds.
- **Dynamic ETA Prediction:** Computes estimated time of arrival to nearest geofence centroid.

### 2.3 On-Device Geofencing Engine
- **Circular & Polygonal Geofences:** Up to 8 configurable zones stored directly in STM32 flash.
- **Ray-Casting Point-in-Polygon Algorithm:** Validates concave, convex, and complex terminal boundaries directly on MCU.
- **Event Dispatching:** Generates `ENTER`, `EXIT`, and `DWELL` events with optional immediate alarm transmission override.

### 2.4 Compact Binary CBOR Protocols
- Telemetry maps encoded into **< 60 bytes** RFC 7049 CBOR frames for rapid transmission over narrow-band cellular links, minimizing modem on-time and power draw.

---

## 3. Hardware Bill of Materials (BOM)

| Designator | Component | Purpose | Package |
|---|---|---|---|
| **U1** | [STM32L4R5ZIT6](file:///hardware/bom.csv) | Ultra-low-power MCU (2MB Flash, 640KB RAM) | LQFP-144 |
| **U2** | [MAX-M10S-00B](file:///hardware/bom.csv) | u-blox GNSS receiver (<5 mA acquisition) | LCC-18 |
| **U3** | [SARA-R410M-02B](file:///hardware/bom.csv) | u-blox NB-IoT / LTE-M cellular modem | LGA-96 |
| **U4** | [LIS2DW12TR](file:///hardware/bom.csv) | Ultra-low-power 3-axis accelerometer (1 µA) | LGA-12 |
| **U5, U6** | [TPS22918DBVR](file:///hardware/bom.csv) | Power gating load switches (2A, controlled slew) | SOT-23-6 |
| **BT1** | [Tadiran TL-5930](file:///hardware/bom.csv) | 3.6 V 19 Ah LiSOCl2 D-cell primary battery | D-Cell |
| **ENC1** | [Hammond 1554](file:///hardware/bom.csv) | IP67 weatherproof polycarbonate enclosure | 120×90×60mm |

See [Full Bill of Materials](file:///hardware/bom.csv), [KiCad Project](file:///hardware/kicad/asset-tracker.kicad_pro), and [PCB Assembly Guide](file:///docs/pcb_assembly_guide.md).

---

## 4. Hardware Pin Mapping (STM32L4R5ZI)

| Signal Name | STM32 Pin | Function | Peripheral |
|---|---|---|---|
| `GNSS_UART_TX` | **PA2** | UBX transmit to GNSS | USART2_TX |
| `GNSS_UART_RX` | **PA3** | UBX receive from GNSS | USART2_RX |
| `GNSS_SW_EN` | **PA4** | High = Turn on GNSS load switch | GPIO Output |
| `MODEM_UART_TX` | **PA9** | AT commands to SARA-R410M | USART1_TX |
| `MODEM_UART_RX` | **PA10** | AT responses from SARA-R410M | USART1_RX |
| `MODEM_SW_EN` | **PA5** | High = Turn on Modem load switch | GPIO Output |
| `ACCEL_SPI_SCK` | **PB5** | SPI clock to LIS2DW12 | SPI1_SCK |
| `ACCEL_SPI_MISO` | **PB6** | SPI data from LIS2DW12 | SPI1_MISO |
| `ACCEL_SPI_MOSI` | **PB7** | SPI data to LIS2DW12 | SPI1_MOSI |
| `ACCEL_CS` | **PB4** | Accelerometer chip select | GPIO Output |
| `ACCEL_INT1` | **PA0** | Wake-on-movement interrupt (WKUP1) | EXTI0 / WKUP1 |
| `STATUS_LED` | **PC13** | Low-current visual beacon | GPIO Output |
| `VBAT_SENSE` | **PA1** | Battery voltage divider sense | ADC1_IN6 |

---

## 5. Directory Structure

```
asset-tracker/
├── README.md                      # Production documentation
├── docker-compose.yaml            # PostGIS + Backend container stack
├── firmware/                      # Bare-metal STM32L4R5 C firmware
│   ├── CMakeLists.txt             # Cross-compilation and host test build
│   ├── STM32L4R5ZITx_FLASH.ld     # Microcontroller linker script
│   ├── startup_stm32l4r5xx.s      # Vector table & Cortex-M4 startup
│   ├── config/                    # Hardware pins & communication settings
│   │   ├── pin_map.h
│   │   ├── power_config.h
│   │   └── comms_config.h
│   ├── src/                       # Application modules
│   │   ├── main.c                 # Primary tracker state loop
│   │   ├── power/                 # Power gating & sleep modes
│   │   ├── gnss/                  # UBX parser & AssistNow ephemeris
│   │   ├── kalman/                # 4-state linear KF & ETA predictor
│   │   ├── geofence/              # Point-in-polygon & zone flash storage
│   │   ├── comms/                 # SARA-R410M driver & CBOR packet encoder
│   │   └── sensors/               # LIS2DW12 accelerometer & die temperature
│   └── tests/                     # Firmware C unit test suite
│       ├── test_kalman.c
│       ├── test_geofence.c
│       ├── test_ubx_parser.c
│       └── test_packet.c
├── backend/                       # Cloud telemetry backend
│   ├── main.py                    # FastAPI app & async UDP listener (5683)
│   ├── api/routes.py              # REST API & WebSocket live streaming
│   ├── models.py                  # SQLAlchemy models & Pydantic schemas
│   ├── database.py                # Database connection & seed data
│   ├── cbor_decoder.py            # RFC 7049 binary telemetry parser
│   ├── requirements.txt           # Python dependencies
│   ├── Dockerfile                 # Production backend container
│   └── tests/test_backend.py      # End-to-end pytest suite
├── dashboard/                     # Web telematics interface
│   ├── index.html                 # Modern glassmorphism UI
│   ├── css/style.css              # Dark theme styling & animations
│   └── js/                        # Frontend modules
│       ├── map.js                 # Leaflet map, pulsing SVG markers & trails
│       ├── tracker.js             # Telemetry state, WebSockets & downlinks
│       └── geofence-editor.js     # Interactive geofence drawing
├── hardware/                      # Schematics, PCB & Manufacturing
│   ├── kicad/                     # KiCad 7/8 PCB & schematic files
│   ├── bom.csv                    # Complete manufacturing Bill of Materials
│   └── enclosure_stl/             # 3D printable IP67 enclosure CAD model
└── docs/                          # Detailed engineering documentation
    ├── power_budget_analysis.md
    ├── deployment_guide.md
    └── pcb_assembly_guide.md
```

---

## 6. Quick Start & Execution

### 6.1 Running the Cloud Backend & Live Dashboard
1. Install dependencies:
   ```bash
   pip install -r backend/requirements.txt
   ```
2. Start the FastAPI backend server:
   ```bash
   uvicorn backend.main:app --host 0.0.0.0 --port 8000 --reload
   ```
3. Open your browser to **`http://localhost:8000/`** to view the live dashboard.
   - Real asset positions, battery telemetry, and history trails load immediately from the database.
   - Use the **"Send Telemetry"** button in the top bar to simulate live incoming hardware datagrams in real-time over WebSockets!

### 6.2 Running Backend Test Suite
```bash
pytest backend/tests/test_backend.py -v
```

### 6.3 Building Firmware for Real Hardware
Prerequisites: `arm-none-eabi-gcc` and `cmake`.

```bash
cd firmware
mkdir build && cd build
cmake -DHAL_DRIVER_PATH=/path/to/STM32CubeL4/Drivers ..
make -j4
```
Flash the binary onto the hardware node:
```bash
STM32_Programmer_CLI -c port=SWD -w asset-tracker.bin 0x08000000 -v -rst
```

---

## 7. License
Distributed under the MIT License. See `LICENSE` for more information.
