# PCB Assembly, Fabrication & Hardware Bring-Up Guide

## 1. PCB Manufacturing Specifications

| Parameter | Specification | Notes |
|---|---|---|
| **Layer Count** | 2 Layers | Top: Signal & Components, Bottom: Ground Plane & Power |
| **Material** | FR4 Standard ($T_g \ge 150\,^{\circ}\text{C}$) | High dimensional stability across wide temperature range |
| **Board Dimensions** | 75.0 mm × 55.0 mm | Rectangular with 3.0 mm corner radii |
| **Board Thickness** | 1.6 mm ± 10% | Standard |
| **Copper Weight** | 1 oz (35 µm) finished | Both layers |
| **Surface Finish** | **ENIG** (Electroless Nickel Immersion Gold) | Mandatory for LGA and fine-pitch QFP pads |
| **Solder Mask** | Matte Black or Dark Blue | LPI solder mask |
| **Silkscreen** | High-definition White | Legible 0.8 mm pin legends |
| **Min Trace / Space** | 0.15 mm / 0.15 mm (6/6 mil) | Standard fab process |
| **Min Drill** | 0.3 mm through-hole | Vias 0.6 mm pad / 0.3 mm hole |
| **RF Impedance** | **50 Ω ± 7% Coplanar Microstrip** | GNSS (J2) and Cellular (J3) antenna feeds |

---

## 2. Assembly & SMT Reflow Guidelines

### 2.1 Stencil & Solder Paste
- **Stencil Thickness:** 0.10 mm (4 mil) stainless steel, laser cut with electropolish.
- **Solder Paste:** Lead-free **SAC305 (Sn96.5 / Ag3.0 / Cu0.5)**, Type 4 powder size for reliable LGA aperture release.

### 2.2 Critical Component Considerations
1. **u-blox SARA-R410M (LGA-96):**
   - Central thermal ground pad array requires 100% paste coverage with cross-hatch stencil apertures to prevent excessive voiding and tilting.
2. **ST LIS2DW12 (LGA-12):**
   - 2.0 mm × 2.0 mm pitch is 0.5 mm. Ensure precision optical alignment. Avoid placing vias directly under package pads.
3. **AVX Tantalum Polymer Capacitor (C1 - 100 µF):**
   - **CRITICAL:** Tantalum capacitors are polarized! The beveled edge / laser marked white stripe indicates the **POSITIVE (+)** terminal. Reversed placement will cause explosive destruction during cell connection.
4. **Tadiran TL-5930 Battery Depassivation:**
   - LiSOCl2 cells build a protective passivation layer during storage. If the open-circuit voltage drops below 3.3 V under an initial 100 mA load, depassivate by connecting a 36 Ω 2W power resistor across the battery for 60 seconds before installing into the tracker.

---

## 3. Step-by-Step Bring-Up & Smoke Test

### Phase 1: Cold Resistance Checks (Unpowered)
Before connecting power, measure resistance with a digital multimeter:
- `+3V6_BATT` to `GND`: **> 100 kΩ** (charging C1 capacitor).
- `+3V3_MCU` to `GND`: **> 50 kΩ**.
- `+3V3_GNSS` (output of U5) to `GND`: **> 100 kΩ**.
- `+3V6_CELL` (output of U6) to `GND`: **> 50 kΩ**.

### Phase 2: First Power Application
1. Set a precision DC bench power supply to **3.60 V** with a current limit of **50 mA**.
2. Connect to battery header `BT1`.
3. Check current draw immediately:
   - **Expected:** Current initially peaks to ~30 mA while decoupling caps charge, then drops to **< 5 mA** in bootloader mode.
   - If current hits the 50 mA limit, cut power immediately and inspect for solder bridges.

### Phase 3: SWD Debugger Connection & Flashing
1. Attach an ST-Link V3 or J-Link to the SWD header (`SWDIO`, `SWCLK`, `GND`, `NRST`).
2. Run `STM32_Programmer_CLI -c port=SWD` to verify target detection:
   ```
   Device name: STM32L4R5xx
   Flash size: 2 MBytes
   SRAM size: 640 KBytes
   ```
3. Flash the compiled firmware binary:
   ```bash
   STM32_Programmer_CLI -c port=SWD -w build/asset-tracker.bin 0x08000000 -v -rst
   ```

### Phase 4: Functional Peripheral Verification
- **UART2 (Cellular):** Check AT command response `AT` -> `OK`.
- **UART1 (GNSS):** Monitor 9600/38400 baud stream for `$GNRMC` or UBX sync headers.
- **SPI1 (Accelerometer):** Verify WHO_AM_I register reads `0x44`.
- **Status LED:** D1 flashes once every 10 seconds indicating normal RTC duty cycle.
