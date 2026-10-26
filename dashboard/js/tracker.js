/**
 * @file tracker.js
 * @brief Telemetry State & WebSocket Controller for AegisTrack Industrial
 */

class TrackerApp {
  constructor() {
    this.mapController = new TrackerMap('map');
    this.trackers = new Map();     // id -> tracker data
    this.selectedTrackerId = null;
    this.filterType = 'all';
    this.searchQuery = '';
    this.ws = null;
    this.reconnectTimer = null;
    this.alerts = [];

    this.initEventListeners();
    this.loadFleetData();
    this.loadAlerts();
    this.connectWebSocket();
  }

  initEventListeners() {
    // Listen for marker click from map
    window.addEventListener('trackerSelected', (e) => {
      this.selectTracker(e.detail.trackerId);
    });

    // Search input
    const searchInput = document.getElementById('search-input');
    if (searchInput) {
      searchInput.addEventListener('input', (e) => {
        this.searchQuery = e.target.value.toLowerCase();
        this.renderTrackerList();
      });
    }

    // Filter tags
    document.querySelectorAll('.tag-btn').forEach(btn => {
      btn.addEventListener('click', (e) => {
        document.querySelectorAll('.tag-btn').forEach(b => b.classList.remove('active'));
        e.target.classList.add('active');
        this.filterType = e.target.dataset.filter;
        this.renderTrackerList();
      });
    });

    // Close detail panel
    const closeBtn = document.getElementById('close-detail-btn');
    if (closeBtn) {
      closeBtn.addEventListener('click', () => {
        const panel = document.getElementById('detail-panel');
        if (panel) panel.classList.add('hidden');
      });
    }

    // Toggle bottom drawer
    const drawerToggle = document.getElementById('drawer-toggle');
    if (drawerToggle) {
      drawerToggle.addEventListener('click', () => {
        const drawer = document.getElementById('bottom-drawer');
        if (drawer) drawer.classList.toggle('collapsed');
      });
    }

    // Fit map bounds button
    const fitBtn = document.getElementById('fit-bounds-btn');
    if (fitBtn) {
      fitBtn.addEventListener('click', () => this.mapController.fitAll());
    }

    // Downlink command send button
    const sendCmdBtn = document.getElementById('send-command-btn');
    if (sendCmdBtn) {
      sendCmdBtn.addEventListener('click', () => this.dispatchDownlinkCommand());
    }

    // Test datagram modal
    const openModalBtn = document.getElementById('open-ingest-modal-btn');
    const closeModalBtn = document.getElementById('close-modal-btn');
    const modal = document.getElementById('ingest-modal');
    const submitDatagramBtn = document.getElementById('submit-datagram-btn');

    if (openModalBtn && modal) {
      openModalBtn.addEventListener('click', () => modal.classList.remove('hidden'));
    }
    if (closeModalBtn && modal) {
      closeModalBtn.addEventListener('click', () => modal.classList.add('hidden'));
    }
    if (submitDatagramBtn) {
      submitDatagramBtn.addEventListener('click', () => this.sendTestDatagram());
    }
  }

  async loadFleetData() {
    try {
      const res = await fetch('/api/v1/trackers');
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();

      this.trackers.clear();
      data.forEach(t => {
        this.trackers.set(t.id, t);
        if (t.latest_position) {
          this.mapController.updateTrackerMarker(t, t.latest_position);
        }
      });

      this.renderTrackerList();
      this.updateFleetMetrics();

      // Automatically select first tracker if none selected
      if (!this.selectedTrackerId && data.length > 0) {
        this.selectTracker(data[0].id);
        this.mapController.fitAll();
      }
    } catch (err) {
      console.error('Failed to load fleet data:', err);
    }
  }

  async loadAlerts() {
    try {
      const res = await fetch('/api/v1/alerts');
      if (res.ok) {
        this.alerts = await res.json();
        this.renderAlertsTable();
      }
    } catch (err) {
      console.error('Failed to load alerts:', err);
    }
  }

  connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = window.location.host || 'localhost:8000';
    const wsUrl = `${protocol}//${host}/ws/live`;

    const statusPill = document.getElementById('connection-status');

    try {
      this.ws = new WebSocket(wsUrl);

      this.ws.onopen = () => {
        console.log('Connected to real-time WebSocket stream');
        if (statusPill) {
          statusPill.innerHTML = '<span class="pulse-dot"></span> Live Telemetry Connected';
          statusPill.style.color = '#10b981';
        }
        if (this.reconnectTimer) {
          clearInterval(this.reconnectTimer);
          this.reconnectTimer = null;
        }
      };

      this.ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);
          if (msg.type === 'POSITION_UPDATE') {
            this.handleLivePositionUpdate(msg);
          }
        } catch (e) {
          console.warn('Non-JSON WebSocket frame:', event.data);
        }
      };

      this.ws.onclose = () => {
        console.warn('WebSocket disconnected. Reconnecting in 3s...');
        if (statusPill) {
          statusPill.innerHTML = '<span class="pulse-dot" style="background:#f59e0b; box-shadow:none;"></span> Reconnecting...';
          statusPill.style.color = '#f59e0b';
        }
        if (!this.reconnectTimer) {
          this.reconnectTimer = setTimeout(() => this.connectWebSocket(), 3000);
        }
      };

      this.ws.onerror = (err) => {
        console.error('WebSocket error:', err);
      };
    } catch (err) {
      console.error('Could not initialize WebSocket:', err);
    }
  }

  handleLivePositionUpdate(msg) {
    const tracker = msg.tracker;
    const pos = msg.position;

    // Update in-memory tracker record
    const existing = this.trackers.get(tracker.id) || tracker;
    existing.status = tracker.status;
    existing.last_seen = tracker.last_seen;
    existing.latest_position = pos;
    this.trackers.set(tracker.id, existing);

    // Update map marker
    this.mapController.updateTrackerMarker(existing, pos);

    // Update list card
    this.renderTrackerList();
    this.updateFleetMetrics();

    // If currently selected, refresh detail panel & polyline
    if (this.selectedTrackerId === tracker.id) {
      this.renderDetailPanel(existing);
      this.fetchAndDrawHistory(tracker.id);
    }

    // If an alert was generated, prepend to alerts
    if (msg.alert) {
      this.alerts.unshift(msg.alert);
      this.renderAlertsTable();
      this.showToastAlert(msg.alert);
    }
  }

  async selectTracker(trackerId) {
    this.selectedTrackerId = trackerId;
    const tracker = this.trackers.get(trackerId);
    if (!tracker) return;

    // Highlight card
    document.querySelectorAll('.tracker-card').forEach(card => {
      card.classList.toggle('active', card.dataset.id === trackerId);
    });

    // Fly to on map
    this.mapController.focusTracker(trackerId);

    // Render detail panel
    this.renderDetailPanel(tracker);

    // Fetch history and geofences
    await this.fetchAndDrawHistory(trackerId);
    await this.fetchAndDrawGeofences(trackerId);
  }

  async fetchAndDrawHistory(trackerId) {
    try {
      const res = await fetch(`/api/v1/trackers/${trackerId}/history`);
      if (res.ok) {
        const positions = await res.json();
        this.mapController.drawTrajectory(trackerId, positions);
      }
    } catch (e) {
      console.error('Failed to load history:', e);
    }
  }

  async fetchAndDrawGeofences(trackerId) {
    try {
      const res = await fetch(`/api/v1/trackers/${trackerId}/geofences`);
      if (res.ok) {
        const geofences = await res.json();
        this.mapController.drawGeofences(geofences);
      }
    } catch (e) {
      console.error('Failed to load geofences:', e);
    }
  }

  renderTrackerList() {
    const listContainer = document.getElementById('tracker-list');
    if (!listContainer) return;

    listContainer.innerHTML = '';

    const filtered = Array.from(this.trackers.values()).filter(t => {
      const matchesType = this.filterType === 'all' || t.type === this.filterType;
      const matchesSearch = !this.searchQuery ||
        t.name.toLowerCase().includes(this.searchQuery) ||
        t.id.toLowerCase().includes(this.searchQuery);
      return matchesType && matchesSearch;
    });

    filtered.forEach(t => {
      const pos = t.latest_position || {};
      const card = document.createElement('div');
      card.className = `tracker-card status-${t.status} ${this.selectedTrackerId === t.id ? 'active' : ''}`;
      card.dataset.id = t.id;

      const batMv = pos.battery_mv || 3600;
      const batPct = Math.min(100, Math.max(0, Math.round(((batMv - 3000) / 600) * 100)));
      const batColor = batMv > 3400 ? '#10b981' : batMv > 3200 ? '#f59e0b' : '#ef4444';

      card.innerHTML = `
        <div class="card-top">
          <div class="card-title">
            <span class="asset-badge">${t.type}</span>
            <h3>${t.name}</h3>
          </div>
          <div class="card-status">
            <span class="status-indicator"></span>
            ${t.status.toUpperCase()}
          </div>
        </div>
        <div class="card-telemetry">
          <div class="telemetry-item">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
            <span class="value">${this.formatRelativeTime(t.last_seen)}</span>
          </div>
          <div class="telemetry-item">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg>
            <span class="value">${pos.speed ? (pos.speed * 3.6).toFixed(1) + ' km/h' : '0.0 km/h'}</span>
          </div>
          <div class="telemetry-item">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"><path d="M14 14.76V3.5a2.5 2.5 0 0 0-5 0v11.26a4.5 4.5 0 1 0 5 0z"/></svg>
            <span class="value">${pos.temperature !== undefined ? pos.temperature + '°C' : '--'}</span>
          </div>
          <div class="telemetry-item">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"><rect x="1" y="6" width="18" height="12" rx="2"/><line x1="23" y1="13" x2="23" y2="11"/></svg>
            <span class="value">${batMv} mV</span>
          </div>
        </div>
        <div class="battery-bar-container">
          <div class="battery-bar" style="width: ${batPct}%; background: ${batColor}"></div>
        </div>
      `;

      card.addEventListener('click', () => this.selectTracker(t.id));
      listContainer.appendChild(card);
    });
  }

  renderDetailPanel(tracker) {
    const panel = document.getElementById('detail-panel');
    if (!panel) return;
    panel.classList.remove('hidden');

    const pos = tracker.latest_position || {};

    document.getElementById('detail-name').textContent = tracker.name;
    document.getElementById('detail-id').textContent = tracker.id;

    // Telemetry fields
    document.getElementById('pos-lat').textContent = pos.latitude ? pos.latitude.toFixed(6) : '--';
    document.getElementById('pos-lon').textContent = pos.longitude ? pos.longitude.toFixed(6) : '--';
    document.getElementById('pos-alt').textContent = pos.altitude ? `${pos.altitude} m` : '0.0 m';
    document.getElementById('pos-spd').textContent = pos.speed ? `${(pos.speed * 3.6).toFixed(1)} km/h` : '0.0 km/h';
    document.getElementById('pos-hdg').textContent = pos.heading ? `${pos.heading.toFixed(0)}°` : '0°';
    document.getElementById('pos-temp').textContent = pos.temperature !== undefined ? `${pos.temperature}°C` : '--';

    // Fix & Kalman
    document.getElementById('pos-fix').textContent = pos.fix_type === 3 ? '3D GNSS Fix' : '2D Fix';
    document.getElementById('pos-hdop').textContent = pos.hdop ? pos.hdop.toFixed(2) : '1.0';
    document.getElementById('pos-eta').textContent = pos.eta_minutes ? `${pos.eta_minutes} mins` : 'N/A';

    // Battery & Multi-Year Power Analysis
    const batMv = pos.battery_mv || 3600;
    const batPct = Math.min(100, Math.max(0, Math.round(((batMv - 3000) / 600) * 100)));
    document.getElementById('pos-bat-mv').textContent = `${batMv} mV (${batPct}%)`;

    // Multi-year calculation based on 19 Ah Tadiran LTC cell & 21 uA avg current
    // 19 Ah * (batPct / 100) / (0.000021 A * 8760 h/yr)
    const remainingYears = ((19.0 * (batPct / 100.0)) / (0.000021 * 8760)).toFixed(1);
    document.getElementById('pos-remaining-runtime').textContent = `~${remainingYears} Years (at 21µA duty)`;
  }

  renderAlertsTable() {
    const tbody = document.getElementById('alerts-tbody');
    if (!tbody) return;

    tbody.innerHTML = '';
    this.alerts.slice(0, 15).forEach(a => {
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td><span class="severity-pill severity-${a.severity}">${a.severity}</span></td>
        <td style="font-family:var(--font-mono); font-weight:600;">${a.tracker_id}</td>
        <td>${a.alert_type}</td>
        <td>${a.message}</td>
        <td style="color:var(--text-muted); font-size:0.75rem;">${this.formatRelativeTime(a.timestamp)}</td>
      `;
      tbody.appendChild(tr);
    });

    const alertCountPill = document.getElementById('active-alerts-count');
    if (alertCountPill) {
      alertCountPill.textContent = this.alerts.length;
    }
  }

  showToastAlert(alert) {
    const container = document.getElementById('toast-container');
    if (!container) return;

    const toast = document.createElement('div');
    toast.className = `metric-pill status-${alert.severity}`;
    toast.style.background = alert.severity === 'critical' ? 'rgba(244,63,94,0.9)' : 'rgba(245,158,11,0.9)';
    toast.style.color = '#ffffff';
    toast.style.padding = '10px 18px';
    toast.style.borderRadius = '8px';
    toast.style.boxShadow = '0 8px 24px rgba(0,0,0,0.5)';
    toast.style.marginBottom = '8px';
    toast.innerHTML = `<b>[${alert.alert_type}]</b> ${alert.tracker_id}: ${alert.message}`;

    container.appendChild(toast);
    setTimeout(() => toast.remove(), 6000);
  }

  updateFleetMetrics() {
    const totalAssets = this.trackers.size;
    let activeMoving = 0;
    let totalBat = 0;

    this.trackers.forEach(t => {
      if (t.latest_position && t.latest_position.speed > 0.5) activeMoving++;
      if (t.latest_position && t.latest_position.battery_mv) totalBat += t.latest_position.battery_mv;
    });

    const avgBat = totalAssets > 0 ? Math.round(totalBat / totalAssets) : 3600;

    const totalEl = document.getElementById('fleet-total');
    if (totalEl) totalEl.textContent = totalAssets;

    const movingEl = document.getElementById('fleet-moving');
    if (movingEl) movingEl.textContent = activeMoving;

    const batEl = document.getElementById('fleet-avg-bat');
    if (batEl) batEl.textContent = `${avgBat} mV`;
  }

  async dispatchDownlinkCommand() {
    if (!this.selectedTrackerId) return;

    const select = document.getElementById('downlink-interval-select');
    const intervalSec = parseInt(select.value, 10);

    try {
      const res = await fetch(`/api/v1/trackers/${this.selectedTrackerId}/command`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          cmd: 'set_interval',
          payload: { value: intervalSec }
        }),
      });

      if (res.ok) {
        alert(`Downlink queued for ${this.selectedTrackerId}: Set interval to ${intervalSec}s. Will deliver on next wake.`);
      } else {
        alert('Failed to queue downlink command.');
      }
    } catch (e) {
      console.error(e);
      alert('Error sending downlink.');
    }
  }

  async sendTestDatagram() {
    const id = document.getElementById('test-tracker-id').value;
    const lat = parseFloat(document.getElementById('test-lat').value);
    const lon = parseFloat(document.getElementById('test-lon').value);
    const spd = parseFloat(document.getElementById('test-spd').value);
    const bat = parseInt(document.getElementById('test-bat').value, 10);
    const geoEvt = parseInt(document.getElementById('test-geo-evt').value, 10);

    const payload = {
      id: id,
      ts: Math.floor(Date.now() / 1000),
      lat: lat,
      lon: lon,
      alt: 15.0,
      spd: spd,
      hdg: 180.0,
      bat: bat,
      temp: 21.0,
      fix: 3,
      hdop: 0.9,
      geo_evt: geoEvt,
      geo_zone: geoEvt > 0 ? 1 : 0,
      anomaly: 0,
      eta_min: 15,
      seq: Math.floor(Math.random() * 10000),
    };

    try {
      const res = await fetch('/api/v1/ingest', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });

      if (res.ok) {
        document.getElementById('ingest-modal').classList.add('hidden');
      } else {
        alert('Ingestion error: ' + (await res.text()));
      }
    } catch (e) {
      console.error(e);
      alert('Network error sending telemetry.');
    }
  }

  formatRelativeTime(isoStr) {
    if (!isoStr) return '--';
    const date = new Date(isoStr);
    const diffSec = Math.floor((new Date() - date) / 1000);
    if (diffSec < 60) return `${diffSec}s ago`;
    if (diffSec < 3600) return `${Math.floor(diffSec / 60)}m ago`;
    if (diffSec < 86400) return `${Math.floor(diffSec / 3600)}h ago`;
    return `${Math.floor(diffSec / 86400)}d ago`;
  }
}

document.addEventListener('DOMContentLoaded', () => {
  window.trackerApp = new TrackerApp();
});
