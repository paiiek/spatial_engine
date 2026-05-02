/**
 * canvas.js — TopDownView + ElevationView for 64 spatial objects.
 * Driven by WsClient messages and requestAnimationFrame at 60 fps.
 */

const MAX_OBJECTS = 64;

// Object state: azim (-180..180 deg), elev (-90..90 deg), dist (0..1), gain (1.0), active
const objects = Array.from({ length: MAX_OBJECTS + 1 }, (_, i) => ({
  id: i,
  azim: 0,
  elev: 0,
  dist: 1.0,
  gain: 1.0,
  active: false,
}));

// Group colour palette (8 groups of 8 objects)
const GROUP_COLORS = [
  "#e74c3c", "#e67e22", "#f1c40f", "#2ecc71",
  "#1abc9c", "#3498db", "#9b59b6", "#e91e63",
];

function groupColor(id) {
  return GROUP_COLORS[Math.floor((id - 1) / 8) % GROUP_COLORS.length];
}

// ─── Canvas setup ────────────────────────────────────────────────────────────

const topCanvas = document.getElementById("topdown-canvas");
const elevCanvas = document.getElementById("elevation-canvas");
const topCtx = topCanvas ? topCanvas.getContext("2d") : null;
const elevCtx = elevCanvas ? elevCanvas.getContext("2d") : null;

function resizeCanvases() {
  if (topCanvas) {
    topCanvas.width = topCanvas.offsetWidth;
    topCanvas.height = topCanvas.offsetHeight;
  }
  if (elevCanvas) {
    elevCanvas.width = elevCanvas.offsetWidth;
    elevCanvas.height = elevCanvas.offsetHeight;
  }
}
window.addEventListener("resize", resizeCanvases);
resizeCanvases();

// ─── Coordinate helpers ───────────────────────────────────────────────────────

/** Convert ADM-OSC azim/dist to canvas (cx, cy) for top-down view. */
function admToTopDown(azim, dist, cx, cy, radius) {
  // ADM-OSC: azim 0=front, +CCW, dist 0=near 1=far
  const rad = (azim * Math.PI) / 180;
  const r = dist * radius;
  return {
    x: cx + r * Math.sin(rad),
    y: cy - r * Math.cos(rad),
  };
}

/** Convert ADM-OSC azim/elev to canvas (ex, ey) for elevation strip. */
function admToElevation(azim, elev, w, h) {
  const ex = ((azim + 180) / 360) * w;
  const ey = ((90 - elev) / 180) * h;
  return { ex, ey };
}

// ─── Draw ─────────────────────────────────────────────────────────────────────

function drawTopDown() {
  if (!topCtx) return;
  const w = topCanvas.width;
  const h = topCanvas.height;
  const cx = w / 2;
  const cy = h / 2;
  const radius = Math.min(w, h) / 2 - 20;

  topCtx.clearRect(0, 0, w, h);

  // Background
  topCtx.fillStyle = "#1a1a2e";
  topCtx.fillRect(0, 0, w, h);

  // Distance rings
  topCtx.strokeStyle = "#2a2a4e";
  topCtx.lineWidth = 1;
  for (let r = 0.25; r <= 1.0; r += 0.25) {
    topCtx.beginPath();
    topCtx.arc(cx, cy, r * radius, 0, Math.PI * 2);
    topCtx.stroke();
  }

  // Azimuth lines every 45°
  topCtx.strokeStyle = "#2a2a4e";
  for (let a = 0; a < 360; a += 45) {
    const rad = (a * Math.PI) / 180;
    topCtx.beginPath();
    topCtx.moveTo(cx, cy);
    topCtx.lineTo(cx + radius * Math.sin(rad), cy - radius * Math.cos(rad));
    topCtx.stroke();
  }

  // Listener (centre)
  topCtx.fillStyle = "#ffffff";
  topCtx.beginPath();
  topCtx.arc(cx, cy, 6, 0, Math.PI * 2);
  topCtx.fill();

  // Objects
  for (let i = 1; i <= MAX_OBJECTS; i++) {
    const obj = objects[i];
    if (!obj.active) continue;
    const { x, y } = admToTopDown(obj.azim, obj.dist, cx, cy, radius);
    const color = groupColor(i);

    topCtx.fillStyle = color;
    topCtx.beginPath();
    topCtx.arc(x, y, 7, 0, Math.PI * 2);
    topCtx.fill();

    topCtx.fillStyle = "#ffffff";
    topCtx.font = "9px monospace";
    topCtx.textAlign = "center";
    topCtx.fillText(String(i), x, y + 3);
  }
}

function drawElevation() {
  if (!elevCtx) return;
  const w = elevCanvas.width;
  const h = elevCanvas.height;

  elevCtx.clearRect(0, 0, w, h);
  elevCtx.fillStyle = "#1a1a2e";
  elevCtx.fillRect(0, 0, w, h);

  // Horizon line
  elevCtx.strokeStyle = "#2a2a4e";
  elevCtx.lineWidth = 1;
  elevCtx.beginPath();
  elevCtx.moveTo(0, h / 2);
  elevCtx.lineTo(w, h / 2);
  elevCtx.stroke();

  // Objects
  for (let i = 1; i <= MAX_OBJECTS; i++) {
    const obj = objects[i];
    if (!obj.active) continue;
    const { ex, ey } = admToElevation(obj.azim, obj.elev, w, h);
    const color = groupColor(i);

    elevCtx.fillStyle = color;
    elevCtx.beginPath();
    elevCtx.arc(ex, ey, 5, 0, Math.PI * 2);
    elevCtx.fill();
  }
}

// ─── Animation loop ──────────────────────────────────────────────────────────

function frame() {
  drawTopDown();
  drawElevation();
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);

// ─── WebSocket message handling ───────────────────────────────────────────────

WsClient.onMessage((data) => {
  const addr = data.osc_address || "";
  const args = data.args || [];

  // /adm/obj/{n}/azim  f
  const azimMatch = addr.match(/^\/adm\/obj\/(\d+)\/azim$/);
  if (azimMatch) {
    const n = parseInt(azimMatch[1], 10);
    if (n >= 1 && n <= MAX_OBJECTS) {
      objects[n].azim = args[0] ?? 0;
      objects[n].active = true;
    }
    return;
  }

  // /adm/obj/{n}/elev  f
  const elevMatch = addr.match(/^\/adm\/obj\/(\d+)\/elev$/);
  if (elevMatch) {
    const n = parseInt(elevMatch[1], 10);
    if (n >= 1 && n <= MAX_OBJECTS) {
      objects[n].elev = args[0] ?? 0;
      objects[n].active = true;
    }
    return;
  }

  // /adm/obj/{n}/dist  f
  const distMatch = addr.match(/^\/adm\/obj\/(\d+)\/dist$/);
  if (distMatch) {
    const n = parseInt(distMatch[1], 10);
    if (n >= 1 && n <= MAX_OBJECTS) {
      objects[n].dist = args[0] ?? 1.0;
      objects[n].active = true;
    }
    return;
  }

  // /adm/obj/{n}/aed  f f f
  const aedMatch = addr.match(/^\/adm\/obj\/(\d+)\/aed$/);
  if (aedMatch) {
    const n = parseInt(aedMatch[1], 10);
    if (n >= 1 && n <= MAX_OBJECTS) {
      objects[n].azim = args[0] ?? 0;
      objects[n].elev = args[1] ?? 0;
      objects[n].dist = args[2] ?? 1.0;
      objects[n].active = true;
    }
    return;
  }
});

// ─── Drag interaction ─────────────────────────────────────────────────────────

let dragging = null;  // { id, startX, startY }

function canvasToAdm(px, py, cx, cy, radius) {
  const dx = px - cx;
  const dy = cy - py;
  const dist = Math.min(Math.hypot(dx, dy) / radius, 1.0);
  const azim = (Math.atan2(dx, dy) * 180) / Math.PI;
  return { azim, dist };
}

function hitTest(px, py) {
  if (!topCanvas) return null;
  const w = topCanvas.width;
  const h = topCanvas.height;
  const cx = w / 2;
  const cy = h / 2;
  const radius = Math.min(w, h) / 2 - 20;

  for (let i = 1; i <= MAX_OBJECTS; i++) {
    const obj = objects[i];
    if (!obj.active) continue;
    const { x, y } = admToTopDown(obj.azim, obj.dist, cx, cy, radius);
    if (Math.hypot(px - x, py - y) <= 10) return i;
  }
  return null;
}

function getCanvasPos(evt, canvas) {
  const rect = canvas.getBoundingClientRect();
  const src = evt.touches ? evt.touches[0] : evt;
  return {
    x: (src.clientX - rect.left) * (canvas.width / rect.width),
    y: (src.clientY - rect.top) * (canvas.height / rect.height),
  };
}

function onPointerDown(evt) {
  evt.preventDefault();
  const { x, y } = getCanvasPos(evt, topCanvas);
  const id = hitTest(x, y);
  if (id !== null) dragging = { id };
}

function onPointerMove(evt) {
  if (!dragging) return;
  evt.preventDefault();
  const { x, y } = getCanvasPos(evt, topCanvas);
  const w = topCanvas.width;
  const h = topCanvas.height;
  const cx = w / 2;
  const cy = h / 2;
  const radius = Math.min(w, h) / 2 - 20;
  const { azim, dist } = canvasToAdm(x, y, cx, cy, radius);
  const n = dragging.id;
  objects[n].azim = azim;
  objects[n].dist = dist;
  // Send to server → OSC 9100
  WsClient.send({ type: "obj_pos", n, azim, elev: objects[n].elev, dist });
}

function onPointerUp(evt) {
  dragging = null;
}

if (topCanvas) {
  topCanvas.addEventListener("mousedown", onPointerDown);
  topCanvas.addEventListener("mousemove", onPointerMove);
  topCanvas.addEventListener("mouseup", onPointerUp);
  topCanvas.addEventListener("mouseleave", onPointerUp);
  topCanvas.addEventListener("touchstart", onPointerDown, { passive: false });
  topCanvas.addEventListener("touchmove", onPointerMove, { passive: false });
  topCanvas.addEventListener("touchend", onPointerUp);
}
