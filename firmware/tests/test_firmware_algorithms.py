"""
Python validation suite matching firmware C algorithms:
- Kalman Filter (Constant Velocity 4-state)
- Point-in-Polygon (Ray-casting)
- UBX binary Fletcher checksum
- CBOR packet serialization
"""

import math
import pytest
import cbor2


# ──────────────────────────── Kalman Filter ────────────────────────────

class KalmanFilter4D:
    def __init__(self, init_lat, init_lon, q_pos=1e-10, q_vel=1e-8, r_pos=7.26e-10):
        self.x = [init_lat, init_lon, 0.0, 0.0]
        self.P = [
            [r_pos, 0, 0, 0],
            [0, r_pos, 0, 0],
            [0, 0, q_vel * 10, 0],
            [0, 0, 0, q_vel * 10]
        ]
        self.Q = [
            [q_pos, 0, 0, 0],
            [0, q_pos, 0, 0],
            [0, 0, q_vel, 0],
            [0, 0, 0, q_vel]
        ]
        self.R = [
            [r_pos, 0],
            [0, r_pos]
        ]

    def predict(self, dt):
        F = [
            [1, 0, dt, 0],
            [0, 1, 0, dt],
            [0, 0, 1, 0],
            [0, 0, 0, 1]
        ]
        # x = F * x
        self.x = [
            F[i][0]*self.x[0] + F[i][1]*self.x[1] + F[i][2]*self.x[2] + F[i][3]*self.x[3]
            for i in range(4)
        ]
        # P = F * P * F^T + Q
        FP = [[sum(F[i][k] * self.P[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
        self.P = [[sum(FP[i][k] * F[j][k] for k in range(4)) + self.Q[i][j] for j in range(4)] for i in range(4)]

    def update(self, z_lat, z_lon):
        y = [z_lat - self.x[0], z_lon - self.x[1]]
        S = [
            [self.P[0][0] + self.R[0][0], self.P[0][1] + self.R[0][1]],
            [self.P[1][0] + self.R[1][0], self.P[1][1] + self.R[1][1]]
        ]
        det = S[0][0] * S[1][1] - S[0][1] * S[1][0]
        assert abs(det) > 1e-30
        inv_det = 1.0 / det
        S_inv = [
            [ S[1][1] * inv_det, -S[0][1] * inv_det],
            [-S[1][0] * inv_det,  S[0][0] * inv_det]
        ]
        # K = P * H^T * S_inv  where H = [I_2 | 0_2]
        K = [[self.P[i][0] * S_inv[0][j] + self.P[i][1] * S_inv[1][j] for j in range(2)] for i in range(4)]

        # x = x + K * y
        for i in range(4):
            self.x[i] += K[i][0] * y[0] + K[i][1] * y[1]

        # P = (I - K*H) * P
        KH = [[K[i][0] if j == 0 else (K[i][1] if j == 1 else 0) for j in range(4)] for i in range(4)]
        IKH = [[(1 if i == j else 0) - KH[i][j] for j in range(4)] for i in range(4)]
        self.P = [[sum(IKH[i][k] * self.P[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def test_kalman_filter_convergence():
    kf = KalmanFilter4D(51.5074, -0.1278)
    # Feed 10 noisy measurements near 51.5074, -0.1278
    noisy = [
        (51.50742, -0.12782), (51.50738, -0.12778),
        (51.50741, -0.12781), (51.50739, -0.12779),
        (51.50740, -0.12780), (51.50741, -0.12781),
        (51.50740, -0.12780), (51.50740, -0.12780),
        (51.50741, -0.12779), (51.50740, -0.12780),
    ]
    for lat, lon in noisy:
        kf.predict(1.0)
        kf.update(lat, lon)

    assert abs(kf.x[0] - 51.5074) < 1e-4
    assert abs(kf.x[1] - (-0.1278)) < 1e-4
    assert abs(kf.x[2]) < 1e-4  # Zero velocity convergence
    assert abs(kf.x[3]) < 1e-4


# ──────────────────────────── Point in Polygon ────────────────────────────

def point_in_polygon(test_lat, test_lon, poly_lats, poly_lons):
    num = len(poly_lats)
    if num < 3:
        return False
    inside = False
    j = num - 1
    for i in range(num):
        yi, xi = poly_lats[i], poly_lons[i]
        yj, xj = poly_lats[j], poly_lons[j]
        if ((yi > test_lat) != (yj > test_lat)) and (test_lon < (xj - xi) * (test_lat - yi) / (yj - yi) + xi):
            inside = not inside
        j = i
    return inside


def test_point_in_polygon_varieties():
    # 1. Square
    sq_lats = [0.0, 0.0, 10.0, 10.0]
    sq_lons = [0.0, 10.0, 10.0, 0.0]
    assert point_in_polygon(5.0, 5.0, sq_lats, sq_lons) is True
    assert point_in_polygon(12.0, 5.0, sq_lats, sq_lons) is False

    # 2. Concave L-Shape
    l_lats = [0.0, 0.0, 4.0, 4.0, 10.0, 10.0]
    l_lons = [0.0, 10.0, 10.0, 4.0, 4.0, 0.0]
    assert point_in_polygon(7.0, 2.0, l_lats, l_lons) is True
    assert point_in_polygon(2.0, 7.0, l_lats, l_lons) is True
    assert point_in_polygon(7.0, 7.0, l_lats, l_lons) is False  # In the cutout notch!


# ──────────────────────────── UBX Checksum ────────────────────────────

def ubx_checksum(data: bytes):
    ck_a = 0
    ck_b = 0
    for b in data:
        ck_a = (ck_a + b) & 0xFF
        ck_b = (ck_b + ck_a) & 0xFF
    return ck_a, ck_b


def test_ubx_fletcher_checksum():
    test_bytes = bytes([0x01, 0x07, 0x00, 0x00])
    ck_a, ck_b = ubx_checksum(test_bytes)
    assert ck_a == 0x08
    assert ck_b == 0x19


# ──────────────────────────── CBOR Round Trip ────────────────────────────

def test_cbor_packet_integrity():
    packet = {
        "id": "TRACKER-001",
        "ts": 1761500000,
        "lat": 515074000,
        "lon": -1278000,
        "alt": 25000,
        "spd": 4200,
        "hdg": 18000000,
        "bat": 3610,
        "temp": -12,
        "fix": 3,
        "hdop": 90,
        "geo_evt": 0,
        "geo_zone": 0,
        "anomaly": 0,
        "seq": 451,
    }
    encoded = cbor2.dumps(packet)
    assert len(encoded) <= 200  # Compact requirement (PACKET_MAX_SIZE = 200)
    decoded = cbor2.loads(encoded)
    assert decoded["id"] == "TRACKER-001"
    assert decoded["temp"] == -12
    assert decoded["bat"] == 3610
