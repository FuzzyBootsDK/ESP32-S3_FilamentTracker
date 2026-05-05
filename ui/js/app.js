/**
 * app.js — Vanilla JS shared utilities.
 * No build step, no framework. Load BEFORE page scripts.
 */

/* ── WebSocket ───────────────────────────────────────────── */
let _ws = null;
let _wsTimer = null;

function connectWS() {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  _ws = new WebSocket(`${proto}//${location.host}/ws`);

  _ws.onopen = () => {
    clearTimeout(_wsTimer);
    document.dispatchEvent(new CustomEvent('ws:open'));
  };
  _ws.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      document.dispatchEvent(new CustomEvent('ws:message', { detail: msg }));
    } catch (_) {}
  };
  _ws.onerror = () => {};
  _ws.onclose = () => {
    document.dispatchEvent(new CustomEvent('ws:close'));
    _wsTimer = setTimeout(connectWS, 4000);
  };
}
connectWS();

/* ── API helpers ─────────────────────────────────────────── */
function _fetchWithTimeout(url, opts, ms = 8000) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), ms);
  return fetch(url, { ...opts, signal: ctrl.signal }).finally(() => clearTimeout(timer));
}

const API = {
  async get(path) {
    const r = await _fetchWithTimeout('/api/v1' + path);
    return r.json();
  },
  async post(path, body) {
    const r = await _fetchWithTimeout('/api/v1' + path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    return r.json();
  },
  async put(path, body) {
    const r = await _fetchWithTimeout('/api/v1' + path, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    return r.json();
  },
  async patch(path, body) {
    const r = await _fetchWithTimeout('/api/v1' + path, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    return r.json();
  },
  async del(path) {
    const r = await _fetchWithTimeout('/api/v1' + path, { method: 'DELETE' });
    return r.json();
  },
  async postRaw(path, body, contentType) {
    const r = await _fetchWithTimeout('/api/v1' + path, {
      method: 'POST',
      headers: { 'Content-Type': contentType },
      body,
    });
    return r.json();
  },
  async postBinary(path, arrayBuffer) {
    const r = await _fetchWithTimeout('/api/v1' + path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: arrayBuffer,
    }, 60000); /* OTA can take a while */
    return r.json();
  },
};

/* ── Toast notifications ─────────────────────────────────── */
let _toastContainer = null;

function toast(message, type = 'success', duration = 3500) {
  if (!_toastContainer) {
    _toastContainer = document.createElement('div');
    _toastContainer.className = 'toast-container';
    document.body.appendChild(_toastContainer);
  }
  const el = document.createElement('div');
  el.className = `toast ${type}`;
  el.textContent = message;
  _toastContainer.appendChild(el);
  setTimeout(() => el.remove(), duration);
}

/* ── Theme management ────────────────────────────────────── */
const _THEMES = ['dark', 'nebula', 'starbucks', 'harmony', 'spring', 'light'];

function applyTheme(theme) {
  if (theme === 'light') theme = 'nebula';
  document.body.classList.remove(..._THEMES);
  document.body.classList.add(theme || 'dark');
  localStorage.setItem('ft_theme', theme || 'dark');
}

/* Apply theme immediately (also done by the inline script in <body>) */
(function () {
  let t = localStorage.getItem('ft_theme') || 'dark';
  if (t === 'light') t = 'nebula';
  document.body.classList.remove(..._THEMES);
  document.body.classList.add(t);
})();

/* ── Debounce helper ─────────────────────────────────────── */
function debounce(fn, delay) {
  let timer;
  return function (...args) {
    clearTimeout(timer);
    timer = setTimeout(() => fn.apply(this, args), delay);
  };
}

/* ── $ / $$ shorthand ────────────────────────────────────── */
function $(sel, ctx) { return (ctx || document).querySelector(sel); }
function $$(sel, ctx) { return Array.from((ctx || document).querySelectorAll(sel)); }

function setText(sel, val, ctx) {
  const el = $(sel, ctx);
  if (el) el.textContent = val ?? '';
}
function setHtml(sel, val, ctx) {
  const el = $(sel, ctx);
  if (el) el.innerHTML = val ?? '';
}
function show(sel, visible, ctx) {
  const el = $(sel, ctx);
  if (el) el.style.display = visible ? '' : 'none';
}
function setClass(sel, cls, on, ctx) {
  const el = $(sel, ctx);
  if (el) el.classList.toggle(cls, !!on);
}

/* ── Sidebar initializer (call once per page) ────────────── */
function initSidebar() {
  /* Active nav highlight */
  const path = location.pathname;
  $$('nav.sidebar a').forEach(a => {
    const href = a.getAttribute('href');
    if (!href) return;
    const match = path === href
      || (href === '/index.html' && (path === '/' || path === ''))
      || (path.endsWith(href));
    a.classList.toggle('active', match);
  });

  const wsDot   = $('.ws-indicator .status-dot');
  const wsLabel = $('.ws-indicator span:last-child');
  const prDot   = $('.printer-status .status-dot');
  const prInfo  = $('.printer-info');
  const prProg  = $('.printer-progress');
  const prBar   = $('.printer-progress-bar');

  let prMeta = $('.printer-meta');
  if (!prMeta) {
    prMeta = document.createElement('div');
    prMeta.className = 'printer-meta';
    const box = $('.sidebar-printer');
    if (box) box.appendChild(prMeta);
  }

  function fmtSidebarEta(mins) {
    if (!(mins > 0)) return 'ETA --';
    const h = Math.floor(mins / 60);
    const m = mins % 60;
    return h ? `ETA ${h}h ${m}m` : `ETA ${m}m`;
  }

  function updatePrinter(d) {
    if (!prDot) return;
    const isPrinting = d.printer_state === 'printing';
    prDot.className = 'status-dot'
      + (isPrinting ? ' printing' : d.printer_online ? ' online' : '');
    if (prInfo) prInfo.textContent = d.printer_online ? d.printer_state : 'Printer offline';
    if (prProg) prProg.style.display = isPrinting ? '' : 'none';
    if (prBar)  prBar.style.width = (d.progress_percent || 0) + '%';

    if (prMeta) {
      if (!d.printer_online) {
        prMeta.textContent = '';
      } else {
        const pct = (d.progress_percent || 0) + '%';
        prMeta.textContent = `${pct} • ${fmtSidebarEta(d.remaining_minutes || 0)}`;
      }
    }
  }

  document.addEventListener('ws:open', () => {
    if (wsDot) wsDot.classList.add('online');
    if (wsLabel) wsLabel.textContent = 'Live';
  });
  document.addEventListener('ws:close', () => {
    if (wsDot) wsDot.classList.remove('online');
    if (wsLabel) wsLabel.textContent = 'Connecting…';
  });
  document.addEventListener('ws:message', e => {
    const msg = e.detail;
    if (msg.type === 'mqtt.runtime.updated') updatePrinter(msg.data);
    if (msg.type === 'settings.updated' && msg.data.theme) applyTheme(msg.data.theme);
  });

  API.get('/mqtt/runtime').then(r => { if (r.ok) updatePrinter(r.data); });
}

/* ── Sort map (inventory) ────────────────────────────────── */
const SORT_MAP = {
  newest:       { sort: 'updated_at',      dir: 'desc' },
  oldest:       { sort: 'updated_at',      dir: 'asc'  },
  remainingAsc: { sort: 'remaining_grams', dir: 'asc'  },
  remainingDesc:{ sort: 'remaining_grams', dir: 'desc' },
  nameAsc:      { sort: 'name',            dir: 'asc'  },
  nameDesc:     { sort: 'name',            dir: 'desc' },
  colorDark:    { sort: 'color_hex',       dir: 'asc'  },
  colorLight:   { sort: 'color_hex',       dir: 'desc' },
};

/* ── Theme definitions (for settings page) ───────────────── */
const THEME_DEFS = [
  { id: 'dark',      name: '🌙 Dark',      swatches: ['#0b1220', '#101a2c', '#3b82f6', '#e6edf7'] },
  { id: 'nebula',    name: '🌌 Nebula',    swatches: ['#141E30', '#1F2A44', '#c084fc', '#ec4899'] },
  { id: 'starbucks', name: '☕ Starbucks', swatches: ['#1e1a16', '#1A322F', '#927E63', '#DED7CF'] },
  { id: 'harmony',   name: '🎵 Harmony',   swatches: ['#08192a', '#0F2A3D', '#FFA439', '#DDEFCE'] },
  { id: 'spring',    name: '🌿 Spring',    swatches: ['#06231D', '#0C342C', '#0db897', '#bce0d6'] },
];

/* ── Tile helpers (inventory) ────────────────────────────── */
function weightPct(item) {
  if (!item.total_grams) return 0;
  return Math.min(100, Math.max(0, Math.round(item.total_remaining_grams / item.total_grams * 100)));
}
function tileStatus(item) {
  const p = weightPct(item);
  if (item.total_grams > 0 && p <= 10) return 'critical';
  if (item.has_low_stock) return 'low';
  return 'ok';
}
function tileBadgeText(s) {
  if (s === 'critical') return 'Critical';
  if (s === 'low') return 'Low';
  return 'OK';
}

/* ── HTML escape ─────────────────────────────────────────── */
function h(str) {
  return String(str ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}
