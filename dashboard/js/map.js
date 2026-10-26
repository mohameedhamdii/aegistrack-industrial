/**
 * @file map.js
 * @brief Interactive Leaflet Map Controller for AegisTrack Industrial
 *
 * Handles dark tile rendering, custom animated SVG asset markers,
 * breadcrumb trajectory trails, and geofence overlays.
 */

class TrackerMap {
  constructor(elementId) {
    this.elementId = elementId;
    this.map = null;
    this.markers = new Map();       // trackerId -> L.Marker
    this.historyLayers = new Map();  // trackerId -> L.Polyline
    this.geofenceLayers = new Map(); // zoneId -> L.Layer
    this.selectedTrackerId = null;

    this.initMap();
  }

  initMap() {
    // Default center on Western Europe (Rotterdam/Antwerp corridor)
    this.map = L.map(this.elementId, {
      center: [51.5, 4.3],
      zoom: 8,
      zoomControl: false,
    });

    // Custom Zoom control position
    L.control.zoom({ position: 'topright' }).addTo(this.map);

    // Dark Matter tile layer for industrial aesthetics
    L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
      attribution: '&copy; <a href="https://carto.com/">CARTO</a> &copy; OpenStreetMap',
      subdomains: 'abcd',
      maxZoom: 19,
    }).addTo(this.map);

    // Invalidate size on container resize
    window.addEventListener('resize', () => {
      if (this.map) this.map.invalidateSize();
    });
  }

  createAssetIcon(type, status, heading = 0) {
    const isContainer = type === 'container';
    const color = status === 'active' ? '#10b981' : status === 'warning' ? '#f59e0b' : '#64748b';
    const bgGlow = status === 'active' ? 'rgba(16, 185, 129, 0.3)' : status === 'warning' ? 'rgba(245, 158, 11, 0.3)' : 'rgba(100, 116, 139, 0.2)';

    const iconSvg = isContainer
      ? `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2">
           <path d="M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z"/>
           <path d="m3.3 7 8.7 5 8.7-5M12 22V12"/>
         </svg>`
      : `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2">
           <path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.41 0-8-3.59-8-8s3.59-8 8-8 8 3.59 8 8-3.59 8-8 8z"/>
           <circle cx="12" cy="12" r="3"/>
         </svg>`;

    return L.divIcon({
      className: 'custom-asset-marker',
      html: `
        <div style="
          width: 38px;
          height: 38px;
          border-radius: 50%;
          background: #161c2e;
          border: 2px solid ${color};
          display: flex;
          align-items: center;
          justify-content: center;
          box-shadow: 0 0 16px ${bgGlow};
          position: relative;
          cursor: pointer;
          transform: rotate(${heading}deg);
        ">
          ${iconSvg}
          <div style="
            position: absolute;
            top: -4px;
            right: -4px;
            width: 10px;
            height: 10px;
            border-radius: 50%;
            background: ${color};
            border: 2px solid #0a0d14;
          "></div>
        </div>
      `,
      iconSize: [38, 38],
      iconAnchor: [19, 19],
    });
  }

  updateTrackerMarker(tracker, position) {
    if (!position || !position.latitude || !position.longitude) return;

    const lat = position.latitude;
    const lon = position.longitude;
    const icon = this.createAssetIcon(tracker.type, tracker.status, position.heading || 0);

    if (this.markers.has(tracker.id)) {
      const marker = this.markers.get(tracker.id);
      marker.setLatLng([lat, lon]);
      marker.setIcon(icon);
    } else {
      const marker = L.marker([lat, lon], { icon }).addTo(this.map);
      marker.on('click', () => {
        window.dispatchEvent(new CustomEvent('trackerSelected', { detail: { trackerId: tracker.id } }));
      });
      this.markers.set(tracker.id, marker);
    }
  }

  drawTrajectory(trackerId, positions) {
    if (!positions || positions.length === 0) return;

    if (this.historyLayers.has(trackerId)) {
      this.map.removeLayer(this.historyLayers.get(trackerId));
    }

    const latlngs = positions.map(p => [p.latitude, p.longitude]);

    const polyline = L.polyline(latlngs, {
      color: '#06b6d4',
      weight: 3,
      opacity: 0.85,
      dashArray: '6, 6',
      lineCap: 'round',
    }).addTo(this.map);

    this.historyLayers.set(trackerId, polyline);
  }

  drawGeofences(geofences) {
    // Clear old geofences
    this.geofenceLayers.forEach(layer => this.map.removeLayer(layer));
    this.geofenceLayers.clear();

    geofences.forEach(geo => {
      let layer = null;

      if (geo.type === 'circular' && geo.center_lat && geo.center_lon) {
        layer = L.circle([geo.center_lat, geo.center_lon], {
          radius: geo.radius_m || 500,
          color: '#3b82f6',
          fillColor: '#3b82f6',
          fillOpacity: 0.12,
          weight: 2,
          dashArray: '5, 5',
        });
      } else if (geo.type === 'polygonal' && geo.vertices && geo.vertices.length >= 3) {
        layer = L.polygon(geo.vertices, {
          color: '#a855f7',
          fillColor: '#a855f7',
          fillOpacity: 0.12,
          weight: 2,
          dashArray: '5, 5',
        });
      }

      if (layer) {
        layer.bindTooltip(`<b>${geo.name}</b> (Zone #${geo.zone_id})`, {
          permanent: false,
          direction: 'top',
          className: 'geofence-tooltip',
        });
        layer.addTo(this.map);
        this.geofenceLayers.set(geo.id || geo.zone_id, layer);
      }
    });
  }

  focusTracker(trackerId) {
    this.selectedTrackerId = trackerId;
    if (this.markers.has(trackerId)) {
      const marker = this.markers.get(trackerId);
      const latlng = marker.getLatLng();
      this.map.flyTo(latlng, 12, { duration: 1.2 });
    }
  }

  fitAll() {
    const group = [];
    this.markers.forEach(m => group.push(m.getLatLng()));
    if (group.length > 0) {
      const bounds = L.latLngBounds(group);
      this.map.fitBounds(bounds, { padding: [60, 60] });
    }
  }
}

window.TrackerMap = TrackerMap;
