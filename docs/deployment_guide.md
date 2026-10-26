# Industrial Field Deployment & Cloud Infrastructure Guide

## 1. Cloud Backend Infrastructure Setup

### 1.1 Prerequisites
- Ubuntu 22.04 LTS or Debian 12 Server
- Docker Engine v24+ and Docker Compose v2+
- Public IPv4 address with UDP port `5683` and TCP port `8000` (or `443`) open in firewall

### 1.2 Deployment with Docker Compose
Clone the repository on your cloud host and launch the production stack:

```bash
git clone https://github.com/your-org/asset-tracker.git /opt/asset-tracker
cd /opt/asset-tracker

# Create production environment file
cat <<EOF > .env
DATABASE_URL=postgresql://postgres:StrongProdPassword123@db:5432/assettracker
UDP_PORT=5683
EOF

# Launch containers
docker compose up -d
```

Verify service health:
```bash
docker compose ps
curl http://localhost:8000/api/v1/trackers
```

### 1.3 Reverse Proxy & SSL (Caddy / Nginx)
For secure HTTPS and WSS (WebSocket Secure) connections to the web dashboard, configure Caddy:

```caddyfile
tracker.yourdomain.com {
    reverse_proxy localhost:8000
}
```

---

## 2. Cellular & SIM Card Provisioning

1. **IoT SIM Selection:** Use low-power global IoT SIM cards supporting NB-IoT (LTE Cat-NB1/NB2) and LTE-M (Cat-M1), such as **1NCE** (10-year flat 500MB rate) or **Hologram**.
2. **APN Configuration:**
   - In `firmware/config/comms_config.h`, ensure `DEFAULT_APN` matches your carrier (e.g., `"iot.1nce.net"` or `"hologram"`).
3. **Band Configuration:** SARA-R410M supports global bands (B1, B2, B3, B4, B5, B8, B12, B13, B18, B19, B20, B26, B28). Ensure roaming agreements are active for remote rangelands and maritime ports.

---

## 3. Physical Device Enclosure & Mounting

### 3.1 IP67 Weatherproofing Procedure
1. Inspect the silicone O-ring gasket in the Hammond 1554 enclosure lid groove. Ensure it is seated with a light film of dielectric silicone grease.
2. Install a **M12 Gore Protective Vent (PMF100412)** to equalize internal and external pressure across altitude/temperature fluctuations without admitting moisture.
3. Torque the four stainless steel lid screws to **0.6 N·m** in a cross pattern.

### 3.2 Container Mounting (Intermodal Shipping)
- Attach using 4× M6 316-grade stainless steel bolts through the outer flanges or two high-strength Neodymium (NdFeB) mounting magnets with 40 kg holding force each.
- Position on the exterior front wall or top corner casting where the patch antenna maintains an unobstructed hemispherical view of the sky.

### 3.3 Livestock Collar Mounting (Cattle & Horses)
- Mount inside a reinforced TPU collar enclosure with UV-stabilized heavy-duty nylon webbing.
- Fit with a mechanical breakaway pin rated at 150 kg to prevent strangulation hazard on fences or low branches.

---

## 4. Commissioning & Pre-Deployment Checklist

- [ ] **Battery Voltage:** Tadiran cell measures > 3.62 V open-circuit.
- [ ] **SIM Installed:** Nano-SIM locked in slot J1; ICCID registered on carrier portal.
- [ ] **GNSS Lock Verification:** Device placed outside achieves 3D fix within 35 s.
- [ ] **NB-IoT Ingestion:** Packet appears on dashboard within 60 s of boot.
- [ ] **Geofence Sync:** Initial geofence boundaries received via downlink.
- [ ] **Sleep Current:** Current drops to < 5 µA in Standby mode.
