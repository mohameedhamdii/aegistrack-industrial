/**
 * @file geofence-editor.js
 * @brief Interactive Geofence Zone Editor for AegisTrack Industrial
 */

class GeofenceEditor {
  constructor() {
    this.isDrawing = false;
    this.drawMode = 'circular'; // 'circular' | 'polygonal'
    this.polygonPoints = [];
    this.tempLayers = [];
    this.initControls();
  }

  initControls() {
    const drawBtn = document.getElementById('btn-draw-geofence');
    const modal = document.getElementById('geofence-modal');
    const closeModal = document.getElementById('close-geo-modal-btn');
    const saveBtn = document.getElementById('save-geofence-btn');

    if (drawBtn && modal) {
      drawBtn.addEventListener('click', () => {
        if (!window.trackerApp || !window.trackerApp.selectedTrackerId) {
          alert('Please select a tracker first before defining a geofence.');
          return;
        }
        modal.classList.remove('hidden');
      });
    }

    if (closeModal && modal) {
      closeModal.addEventListener('click', () => {
        modal.classList.add('hidden');
        this.cancelDrawing();
      });
    }

    if (saveBtn) {
      saveBtn.addEventListener('click', () => this.saveGeofence());
    }

    // Geofence type change
    const typeSelect = document.getElementById('geo-type-select');
      typeSelect.addEventListener('change', (e) => {
        this.drawMode = e.target.value;
        const radiusGroup = document.getElementById('geo-radius-group');
        const centerGroup = document.getElementById('geo-center-group');
        const vertTextarea = document.getElementById('geo-vertices-text');
        const vertGroup = vertTextarea ? vertTextarea.parentElement : null;

        if (radiusGroup) radiusGroup.style.display = this.drawMode === 'circular' ? 'flex' : 'none';
        if (centerGroup) centerGroup.style.display = this.drawMode === 'circular' ? 'grid' : 'none';
        if (vertGroup) vertGroup.style.display = this.drawMode === 'polygonal' ? 'flex' : 'none';
      });
  }

  cancelDrawing() {
    this.isDrawing = false;
    this.polygonPoints = [];
    if (window.trackerApp && window.trackerApp.mapController && window.trackerApp.mapController.map) {
      this.tempLayers.forEach(l => window.trackerApp.mapController.map.removeLayer(l));
    }
    this.tempLayers = [];
  }

  async saveGeofence() {
    if (!window.trackerApp || !window.trackerApp.selectedTrackerId) return;

    const trackerId = window.trackerApp.selectedTrackerId;
    const zoneId = parseInt(document.getElementById('geo-zone-id').value, 10);
    const name = document.getElementById('geo-name').value;
    const type = document.getElementById('geo-type-select').value;
    const dwellTime = parseInt(document.getElementById('geo-dwell-time').value, 10);
    const triggerImmediate = document.getElementById('geo-trigger-tx').checked;

    let payload = {
      zone_id: zoneId,
      name: name,
      type: type,
      dwell_time_s: dwellTime,
      trigger_immediate: triggerImmediate,
    };

    if (type === 'circular') {
      const lat = parseFloat(document.getElementById('geo-center-lat').value);
      const lon = parseFloat(document.getElementById('geo-center-lon').value);
      const radius = parseFloat(document.getElementById('geo-radius-m').value);

      if (isNaN(lat) || isNaN(lon) || isNaN(radius)) {
        alert('Please provide valid center latitude, longitude, and radius.');
        return;
      }
      payload.center_lat = lat;
      payload.center_lon = lon;
      payload.radius_m = radius;
    } else {
      // Polygonal
      const polyText = document.getElementById('geo-vertices-text').value;
      try {
        const vertices = JSON.parse(polyText);
        if (!Array.isArray(vertices) || vertices.length < 3) {
          throw new Error('Polygon must contain at least 3 [lat, lon] vertices');
        }
        payload.vertices = vertices;
      } catch (e) {
        alert('Invalid polygon vertices JSON. Example: [[51.5, 4.2], [51.6, 4.3], [51.4, 4.4]]');
        return;
      }
    }

    try {
      const res = await fetch(`/api/v1/trackers/${trackerId}/geofences`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });

      if (res.ok) {
        document.getElementById('geofence-modal').classList.add('hidden');
        await window.trackerApp.fetchAndDrawGeofences(trackerId);
        alert(`Geofence "${name}" saved and queued for device downlink synchronization!`);
      } else {
        alert('Failed to save geofence: ' + (await res.text()));
      }
    } catch (e) {
      console.error(e);
      alert('Error saving geofence.');
    }
  }
}

document.addEventListener('DOMContentLoaded', () => {
  window.geofenceEditor = new GeofenceEditor();
});
