'use strict';

// ── Config ────────────────────────────────────────────────────────────────────

const DEVICE_ID   = 'gecko-001';
const REFRESH_MS  = 30_000;   // auto-refresh state/power/energy every 30 s
const CHART_MS    = 300_000;  // re-fetch history chart every 5 min

// ── API helpers ───────────────────────────────────────────────────────────────

const api = (path) => `/api/device/${DEVICE_ID}${path}`;

async function apiFetch(path, opts = {}) {
  const r = await fetch(api(path), opts);
  if (!r.ok) {
    const text = await r.text().catch(() => '');
    throw new Error(`${r.status} ${text || r.statusText}`);
  }
  return r.json();
}

// ── DOM helpers ───────────────────────────────────────────────────────────────

const el    = (id) => document.getElementById(id);
const setText = (id, v) => { const e = el(id); if (e) e.textContent = v; };

// ── Labels ────────────────────────────────────────────────────────────────────

const STATE_LABELS = {
  pvOn:   'PV ON',
  acOn:   'AC ON',
  onTemp: 'SATISFIED',
  off:    'OFF',
};
const STATE_CLASSES = {
  pvOn:   'badge badge-pv',
  acOn:   'badge badge-ac',
  onTemp: 'badge badge-ok',
  off:    'badge badge-off',
};
const MODE_LABELS = {
  pvOnly: 'PV Only',
  acOnly: 'AC Only',
  PVAC:   'PV + AC',
  off:    'Off',
};

// ── Render: state ─────────────────────────────────────────────────────────────

function renderState(d) {
  setText('temp-value', d.temperature != null ? `${d.temperature.toFixed(1)} °C` : '—');

  const sb = el('state-badge');
  if (sb) {
    sb.textContent = STATE_LABELS[d.state] ?? d.state ?? '—';
    sb.className   = STATE_CLASSES[d.state] ?? 'badge badge-off';
  }

  setText('mode-value', MODE_LABELS[d.mode] ?? d.mode ?? '—');

  const ob = el('online-badge');
  if (ob) {
    ob.textContent = d.online ? 'Online' : 'Offline';
    ob.className   = `status-badge ${d.online ? 'status-online' : 'status-offline'}`;
  }

  if (d.lastSeen) {
    const age = Math.floor((Date.now() - d.lastSeen) / 1000);
    setText('last-seen', age < 60 ? `${age}s ago` : age < 3600 ? `${Math.floor(age / 60)}m ago` : `${Math.floor(age / 3600)}h ago`);
  }
}

// ── Render: power ─────────────────────────────────────────────────────────────

function renderPower(d) {
  setText('power-value',   `${Math.round(d.power)} W`);
  setText('current-value', `${d.current.toFixed(1)} A`);
  setText('voltage-ac',    `${Math.round(d.voltage)}`);
  setText('voltage-pv',    `${Math.round(d.pvVoltage)}`);
}

// ── Render: energy ────────────────────────────────────────────────────────────

let _kwhRate = 0;

function renderEnergy(d) {
  setText('kwh-session', d.kwhour.toFixed(3));
  setText('kwh-day',     d.kwhDay.toFixed(3));
  setText('kwh-week',    d.kwhWeek.toFixed(3));
  setText('kwh-all',     d.kwhAll.toFixed(3));

  const sc = el('savings-card');
  if (sc && _kwhRate > 0) {
    setText('savings-value', `R ${(d.kwhAll * _kwhRate).toFixed(2)}`);
    sc.style.display = '';
  }
}

// ── Render: settings ──────────────────────────────────────────────────────────

function renderSettings(cfg) {
  el('mode-select').value       = cfg.mode       ?? 'off';
  el('ac-threshold').value      = cfg.acThreshold ?? 55;
  el('ac-threshold-num').value  = cfg.acThreshold ?? 55;
  el('pv-threshold').value      = cfg.pvThreshold ?? 65;
  el('pv-threshold-num').value  = cfg.pvThreshold ?? 65;
  el('kwh-rate').value          = cfg.kwhRate     ?? 0;

  _kwhRate = cfg.kwhRate ?? 0;

  const timers = cfg.timers ?? {};
  for (let n = 1; n <= 4; n++) {
    renderTimerRow(n, timers[`timer${n}`] ?? null);
  }
}

const DAYS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];

function renderTimerRow(n, t) {
  const enabled = t?.enabled  ?? false;
  const sH      = t?.startHour ?? 6;
  const sM      = t?.startMin  ?? 0;
  const eH      = t?.endHour   ?? 8;
  const eM      = t?.endMin    ?? 0;
  const mask    = t?.dayMask   ?? 0x7F;

  el(`timer${n}-enabled`).checked = enabled;
  el(`timer${n}-start`).value = `${String(sH).padStart(2,'0')}:${String(sM).padStart(2,'0')}`;
  el(`timer${n}-end`).value   = `${String(eH).padStart(2,'0')}:${String(eM).padStart(2,'0')}`;

  for (let d = 0; d < 7; d++) {
    const cb = el(`timer${n}-day${d}`);
    if (cb) cb.checked = !!(mask & (1 << d));
  }

  updateTimerRowOpacity(n, enabled);
}

function updateTimerRowOpacity(n, enabled) {
  el(`timer${n}-row`)?.classList.toggle('disabled', !enabled);
}

function buildTimerRows() {
  const container = el('timers-container');
  if (!container) return;
  container.innerHTML = '';

  for (let n = 1; n <= 4; n++) {
    const row = document.createElement('div');
    row.className = 'timer-row disabled';
    row.id = `timer${n}-row`;

    // Title + enable toggle
    const header = document.createElement('div');
    header.style.cssText = 'display:flex;align-items:center;gap:12px;flex-shrink:0';

    const title = document.createElement('div');
    title.className = 'timer-title';
    title.textContent = `Timer ${n}`;

    const enableLabel = document.createElement('label');
    enableLabel.className = 'timer-enable';
    const enableCb = document.createElement('input');
    enableCb.type = 'checkbox';
    enableCb.id = `timer${n}-enabled`;
    enableCb.addEventListener('change', (e) => updateTimerRowOpacity(n, e.target.checked));
    enableLabel.appendChild(enableCb);
    enableLabel.appendChild(document.createTextNode('Enable'));

    header.appendChild(title);
    header.appendChild(enableLabel);

    // Time range
    const times = document.createElement('div');
    times.className = 'timer-times';

    const startIn = document.createElement('input');
    startIn.type = 'time'; startIn.id = `timer${n}-start`; startIn.value = '06:00';
    const endIn   = document.createElement('input');
    endIn.type = 'time';   endIn.id   = `timer${n}-end`;   endIn.value   = '08:00';
    const arrow = document.createElement('span');
    arrow.textContent = '→';

    times.appendChild(startIn);
    times.appendChild(arrow);
    times.appendChild(endIn);

    // Day checkboxes
    const daysDiv = document.createElement('div');
    daysDiv.className = 'days-row';

    DAYS.forEach((day, i) => {
      const lbl = document.createElement('label');
      lbl.className = 'day-label';
      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.id = `timer${n}-day${i}`;
      cb.checked = true;
      const span = document.createElement('span');
      span.textContent = day.slice(0, 2);
      lbl.appendChild(cb);
      lbl.appendChild(span);
      daysDiv.appendChild(lbl);
    });

    row.appendChild(header);
    row.appendChild(times);
    row.appendChild(daysDiv);
    container.appendChild(row);
  }
}

// ── Collect settings from form ────────────────────────────────────────────────

function collectSettings() {
  const mode        = el('mode-select').value;
  const acThreshold = parseFloat(el('ac-threshold-num').value);
  const pvThreshold = parseFloat(el('pv-threshold-num').value);
  const kwhRate     = parseFloat(el('kwh-rate').value) || 0;

  const timers = {};
  for (let n = 1; n <= 4; n++) {
    const enabled = el(`timer${n}-enabled`).checked;
    const [sH, sM] = (el(`timer${n}-start`).value || '00:00').split(':').map(Number);
    const [eH, eM] = (el(`timer${n}-end`).value   || '00:00').split(':').map(Number);

    let mask = 0;
    for (let d = 0; d < 7; d++) {
      if (el(`timer${n}-day${d}`)?.checked) mask |= (1 << d);
    }

    timers[`timer${n}`] = {
      enabled,
      startHour: sH, startMin: sM,
      endHour:   eH, endMin:   eM,
      dayMask:   mask,
    };
  }

  return { mode, acThreshold, pvThreshold, kwhRate, timers };
}

// ── Chart ─────────────────────────────────────────────────────────────────────

let chart = null;

function initChart() {
  const ctx = el('history-chart')?.getContext('2d');
  if (!ctx) return;

  chart = new Chart(ctx, {
    type: 'line',
    data: {
      datasets: [
        {
          label: 'Temperature (°C)',
          data: [],
          borderColor: '#00d4aa',
          backgroundColor: 'rgba(0,212,170,0.08)',
          yAxisID: 'yTemp',
          pointRadius: 0,
          borderWidth: 2,
          tension: 0.35,
          fill: true,
        },
        {
          label: 'Power (W)',
          data: [],
          borderColor: '#f5a623',
          backgroundColor: 'rgba(245,166,35,0.08)',
          yAxisID: 'yPwr',
          pointRadius: 0,
          borderWidth: 2,
          tension: 0.15,
          fill: true,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: 'index', intersect: false },
      plugins: {
        legend: {
          labels: { color: '#c9d1d9', font: { size: 12 } },
        },
        tooltip: {
          backgroundColor: '#21262d',
          titleColor: '#f0f6fc',
          bodyColor: '#c9d1d9',
          borderColor: '#30363d',
          borderWidth: 1,
          callbacks: {
            label: (ctx) => {
              if (ctx.datasetIndex === 0) return ` ${ctx.parsed.y.toFixed(1)} °C`;
              return ` ${Math.round(ctx.parsed.y)} W`;
            },
          },
        },
      },
      scales: {
        x: {
          type: 'time',
          time: { unit: 'hour', displayFormats: { hour: 'HH:mm' } },
          ticks: { color: '#6e7681', maxTicksLimit: 9 },
          grid: { color: '#21262d' },
        },
        yTemp: {
          type: 'linear',
          position: 'left',
          ticks: {
            color: '#00d4aa',
            callback: (v) => `${v} °C`,
          },
          grid: { color: '#21262d' },
          title: { display: false },
        },
        yPwr: {
          type: 'linear',
          position: 'right',
          ticks: {
            color: '#f5a623',
            callback: (v) => `${v} W`,
          },
          grid: { display: false },
        },
      },
    },
  });
}

function renderChart(data) {
  if (!chart || !data?.data) return;
  chart.data.datasets[0].data = data.data.map((d) => ({ x: d.t, y: d.tmp }));
  chart.data.datasets[1].data = data.data.map((d) => ({ x: d.t, y: d.pwr }));
  chart.update('none'); // skip animation on data update
}

// ── Toast ─────────────────────────────────────────────────────────────────────

let _toastTimer = null;

function showToast(msg, type = 'info') {
  const t = el('toast');
  if (!t) return;
  clearTimeout(_toastTimer);
  t.textContent = msg;
  t.className = `toast toast-${type} show`;
  _toastTimer = setTimeout(() => t.classList.remove('show'), 4000);
}

// ── Refresh cycle ─────────────────────────────────────────────────────────────

let _lastRefreshMs = 0;

async function refresh() {
  const btn = el('refresh-btn');
  btn?.classList.add('spinning');

  try {
    const [stateR, powerR, energyR] = await Promise.allSettled([
      apiFetch('/state'),
      apiFetch('/power'),
      apiFetch('/energy'),
    ]);

    if (stateR.status  === 'fulfilled') renderState(stateR.value);
    if (powerR.status  === 'fulfilled') renderPower(powerR.value);
    if (energyR.status === 'fulfilled') renderEnergy(energyR.value);

    _lastRefreshMs = Date.now();
    updateRefreshLabel();
  } catch (e) {
    console.error('refresh error:', e);
  } finally {
    btn?.classList.remove('spinning');
  }
}

async function refreshChart() {
  try {
    const data = await apiFetch('/history');
    renderChart(data);
  } catch (e) {
    console.error('chart refresh error:', e);
  }
}

async function loadSettings() {
  try {
    const cfg = await apiFetch('/settings');
    renderSettings(cfg);
  } catch (e) {
    console.error('settings load error:', e);
    showToast('Could not load settings', 'error');
  }
}

function updateRefreshLabel() {
  if (!_lastRefreshMs) return;
  const age = Math.floor((Date.now() - _lastRefreshMs) / 1000);
  setText('refresh-time', `Updated ${age}s ago`);
}

// ── Event handlers ────────────────────────────────────────────────────────────

async function onSave() {
  const btn = el('save-btn');
  btn.disabled = true;
  btn.textContent = 'Saving…';
  try {
    const payload = collectSettings();
    await apiFetch('/settings', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    _kwhRate = payload.kwhRate;
    showToast('Settings saved ✓', 'success');
    refresh();
  } catch (e) {
    showToast(`Save failed: ${e.message}`, 'error');
  } finally {
    btn.disabled = false;
    btn.textContent = 'Save Settings';
  }
}

async function onReset() {
  if (!confirm('Send a reboot command to the ESP32?\n\nThe device will restart within 60 seconds (on its next settings poll).')) return;

  const btn = el('reset-btn');
  btn.disabled = true;
  btn.textContent = 'Sending…';
  try {
    await apiFetch('/reset', { method: 'POST' });
    showToast('Reboot command sent — device restarts within 60 s', 'success');
  } catch (e) {
    showToast(`Reset failed: ${e.message}`, 'error');
  } finally {
    btn.disabled = false;
    btn.textContent = 'Reboot ESP32';
  }
}

// ── Init ──────────────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
  // Build dynamic timer rows
  buildTimerRows();

  // Sync sliders ↔ number inputs
  ['ac', 'pv'].forEach((p) => {
    const slider = el(`${p}-threshold`);
    const num    = el(`${p}-threshold-num`);
    if (!slider || !num) return;
    slider.addEventListener('input', () => { num.value = slider.value; });
    num.addEventListener('input',   () => { slider.value = num.value; });
  });

  // Button listeners
  el('save-btn')?.addEventListener('click', onSave);
  el('reset-btn')?.addEventListener('click', onReset);
  el('refresh-btn')?.addEventListener('click', () => { refresh(); refreshChart(); });
  el('refresh-settings-btn')?.addEventListener('click', loadSettings);

  // Init chart
  initChart();

  // Initial data fetch (all in parallel)
  Promise.all([refresh(), refreshChart(), loadSettings()]);

  // Periodic auto-refresh
  setInterval(refresh,      REFRESH_MS);
  setInterval(refreshChart, CHART_MS);
  setInterval(updateRefreshLabel, 10_000);
});
