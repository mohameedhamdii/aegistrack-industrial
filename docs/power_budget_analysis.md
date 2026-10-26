# Multi-Year Power Budget Analysis & Longevity Calculation

## 1. Executive Summary

The AegisTrack Industrial Asset Tracker is engineered to operate autonomously for **2 to 5+ years** in harsh outdoor industrial environments (refrigerated intermodal shipping containers, rail cars, and free-range livestock herds) on a single primary battery cell.

Primary energy source: **Tadiran TL-5930 D-Cell Lithium Thionyl Chloride ($\text{LiSOCl}_2$)**
- Nominal Voltage: **3.6 V**
- Nominal Capacity: **19.0 Ah (19,000 mAh)** at 25 °C down to 2.0 V cutoff
- Self-Discharge: **< 1% per year** at 20 °C

---

## 2. Power Consumption by Operating State

| Operating State | MCU Mode | Peripherals Active | Typical Current | Duration per Event | Frequency |
|---|---|---|---|---|---|
| **Deep Standby (Stationary)** | STM32 Standby (RTC ON) | LIS2DW12 Accel in LP mode | **1.6 µA** | Continuous (99.8% of time) | Always-on background |
| **Motion Detection Wake** | Run mode (16 MHz) | LIS2DW12 SPI read (10 ms) | 1.8 mA | 10 ms | On EXTI1 interrupt |
| **GNSS Hot-Start Fix** | Run mode (64 MHz) | MAX-M10S (ephemeris valid) | **5.0 mA** | 3.0 s | Every 6 hours (or 15 min if moving) |
| **GNSS Cold-Start Fix** | Run mode (64 MHz) | MAX-M10S (searching satellites) | 12.0 mA | 30.0 s | Initial power-on or > 4h cold |
| **Cellular Modem Registration** | Run mode (64 MHz) | SARA-R410M CEREG search | 65.0 mA | 4.0 s | 1× per transmission window |
| **Cellular Modem UDP TX** | Run mode (64 MHz) | SARA-R410M RF PA at +23 dBm | **230.0 mA** | 2.5 s | Transmitting 60-byte CBOR packet |
| **Modem PSM (Power Saving Mode)**| Standby | SARA-R410M T3412 periodic TAU | **3.0 µA** | Inactive intervals | Between scheduled transmissions |

---

## 3. Mathematical Power Budget Formulation

### 3.1 Stationary Profile (6-Hour Transmission Interval)
In stationary container or livestock resting mode, fixes and transmissions occur once every 6 hours (4 events per day = 1,460 events per year):

1. **Continuous Baseline (Standby + PSM + Accelerometer):**
   $$I_{\text{baseline}} = I_{\text{MCU\_Standby}} (0.6\,\mu\text{A}) + I_{\text{LIS2DW12}} (1.0\,\mu\text{A}) + I_{\text{Modem\_PSM}} (3.0\,\mu\text{A}) = 4.6\,\mu\text{A}$$
   $$\text{Daily Charge}_{\text{baseline}} = 4.6\,\mu\text{A} \times 24\,\text{h} = 0.1104\,\text{mAh/day}$$

2. **GNSS Fix Acquisition (Hot Start with AssistNow Offline):**
   $$Q_{\text{gnss}} = 5.0\,\text{mA} \times \frac{3\,\text{s}}{3600\,\text{s/h}} = 0.00417\,\text{mAh/fix}$$
   $$\text{Daily Charge}_{\text{gnss}} = 4 \times 0.00417 = 0.0167\,\text{mAh/day}$$

3. **NB-IoT Cellular Transmission (Registration + UDP Socket TX):**
   $$Q_{\text{comms}} = \left(65\,\text{mA} \times \frac{4\,\text{s}}{3600} \right) + \left(230\,\text{mA} \times \frac{2.5\,\text{s}}{3600} \right) = 0.0722 + 0.1597 = 0.2319\,\text{mAh/tx}$$
   $$\text{Daily Charge}_{\text{comms}} = 4 \times 0.2319 = 0.9276\,\text{mAh/day}$$

4. **Total Daily Consumption & Average Current:**
   $$\text{Total Daily Charge} = 0.1104 + 0.0167 + 0.9276 = 1.0547\,\text{mAh/day}$$
   $$I_{\text{avg}} = \frac{1.0547\,\text{mAh}}{24\,\text{h}} = \mathbf{43.9\,\mu\text{A}}$$

---

## 4. Real-World Deratings and Lifetime Projections

Theoretical cell life without environmental degradation:
$$\text{Theoretical Life} = \frac{19,000\,\text{mAh}}{1.0547\,\text{mAh/day}} = 18,014\,\text{days} \approx \mathbf{49.3\,\text{years}}$$

### Real-World Derating Factors:
1. **Passivation Voltage Drop & High Pulse Discharge Efficiency:** 85% usable capacity.
2. **Cold Temperature Operation (-20 °C winter shipping):** 80% capacity multiplier.
3. **Annual Self-Discharge:** ~1.0% per annum.
4. **Moving Transit Bursts (15-min intervals during 10% of life):** 2.2× consumption factor.

$$\text{Practical Available Energy} = 19,000\,\text{mAh} \times 0.85 \times 0.80 = \mathbf{12,920\,\text{mAh}}$$
$$\text{Weighted Average Daily Consumption} = 1.055 \times (0.90) + (1.055 \times 8 \times 0.10) = 1.793\,\text{mAh/day}$$
$$\text{Practical Field Runtime} = \frac{12,920\,\text{mAh}}{1.793\,\text{mAh/day}} = 7,205\,\text{days} \approx \mathbf{19.7\,\text{years (Battery Shelf Limited to 10-15 Years)}}.$$

---

## 5. Verification & Measurement Guide

To validate power consumption against this specification:
1. Connect a **Nordic Power Profiler Kit II (PPK2)** or **Keysight N6705C DC Power Analyzer** in Ampere meter mode between the positive terminal of the Tadiran battery and the board `+3V6_BATT` input.
2. Configure PPK2 sampling rate to **100 kHz**.
3. Verify the idle baseline trace settles at **< 5.0 µA** within 10 seconds of boot.
4. Trigger an accelerometer shake interrupt to confirm wake pulse is bounded to `< 15 ms`.
5. Capture GNSS acquisition current peak and verify load switch isolates GNSS rail to `0.0 µA` when fix is attained.
