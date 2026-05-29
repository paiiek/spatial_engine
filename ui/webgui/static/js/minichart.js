/**
 * minichart.js — self-hosted, dependency-free time-series canvas renderer.
 *
 * v0.9 Lane A · A-M4 · DD-C (LOCKED): zero external JS / CDN. Draws a rolling
 * 60-second window of one or more series with axes, a faint grid, a line per
 * series and an optional peak marker. ~150 LOC, no allocations in the hot
 * render path beyond the small per-frame arrays the canvas API needs.
 *
 * API
 * ---
 *   const chart = new MiniChart(canvasEl, {
 *     windowSec: 60,            // rolling time window
 *     yMax: 100, yMin: 0,       // fixed axis, or null/undefined for auto
 *     series: [                 // one or more named series
 *       { key: "cpu",  color: "#5cf", label: "CPU %" },
 *       { key: "peak", color: "#f80", label: "peak %" },
 *     ],
 *     yUnit: "%",
 *   });
 *   chart.push({ cpu: 12, peak: 30 });   // one sample (now); missing keys held
 *   chart.render();                       // draw current state
 *
 * Each push() stamps the sample with performance.now(); render() drops points
 * older than windowSec so the window scrolls. push() does NOT auto-render —
 * the caller batches a render() per animation frame.
 */

class MiniChart {
  constructor(canvas, opts = {}) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.windowSec = opts.windowSec || 60;
    this.yMin = opts.yMin != null ? opts.yMin : 0;
    this.yMax = opts.yMax != null ? opts.yMax : null; // null → auto-scale
    this.yUnit = opts.yUnit || "";
    this.series = (opts.series || []).map((s) => ({
      key: s.key,
      color: s.color || "#5cf",
      label: s.label || s.key,
      points: [], // [{ t: ms, v: number }]
    }));
    // Visual constants — kept in sync with index.html's dark palette.
    this.bg = opts.bg || "#0f0f1a";
    this.grid = opts.grid || "#1f1f3a";
    this.axis = opts.axis || "#555";
    this.padL = 38;
    this.padR = 8;
    this.padT = 8;
    this.padB = 16;
  }

  /** Append one timestamped sample. ``sample`` maps series key → value. */
  push(sample) {
    const now = performance.now();
    const cutoff = now - this.windowSec * 1000;
    for (const s of this.series) {
      const v = sample[s.key];
      if (v == null || Number.isNaN(Number(v))) continue;
      s.points.push({ t: now, v: Number(v) });
      // Drop expired points (window scroll). Cheap since points are ordered.
      while (s.points.length && s.points[0].t < cutoff) s.points.shift();
    }
  }

  /** Current displayed y-range (respecting fixed yMax or auto-scaling). */
  _yRange() {
    if (this.yMax != null) return [this.yMin, this.yMax];
    let max = this.yMin + 1;
    for (const s of this.series) {
      for (const p of s.points) if (p.v > max) max = p.v;
    }
    return [this.yMin, max * 1.1]; // 10% headroom in auto mode
  }

  render() {
    const ctx = this.ctx;
    const W = this.canvas.width;
    const H = this.canvas.height;
    const x0 = this.padL;
    const x1 = W - this.padR;
    const y0 = this.padT;
    const y1 = H - this.padB;
    const plotW = Math.max(1, x1 - x0);
    const plotH = Math.max(1, y1 - y0);

    // Background
    ctx.fillStyle = this.bg;
    ctx.fillRect(0, 0, W, H);

    const [yLo, yHi] = this._yRange();
    const span = Math.max(1e-6, yHi - yLo);
    const now = performance.now();
    const tStart = now - this.windowSec * 1000;

    const xAt = (t) => x0 + ((t - tStart) / (this.windowSec * 1000)) * plotW;
    const yAt = (v) => y1 - ((v - yLo) / span) * plotH;

    // Grid + y labels (4 horizontal divisions).
    ctx.strokeStyle = this.grid;
    ctx.fillStyle = this.axis;
    ctx.lineWidth = 1;
    ctx.font = "9px monospace";
    ctx.textAlign = "right";
    ctx.textBaseline = "middle";
    for (let i = 0; i <= 4; i++) {
      const gv = yLo + (span * i) / 4;
      const gy = yAt(gv);
      ctx.beginPath();
      ctx.moveTo(x0, gy);
      ctx.lineTo(x1, gy);
      ctx.stroke();
      ctx.fillText(gv.toFixed(0) + this.yUnit, x0 - 4, gy);
    }

    // Axes
    ctx.strokeStyle = this.axis;
    ctx.beginPath();
    ctx.moveTo(x0, y0);
    ctx.lineTo(x0, y1);
    ctx.lineTo(x1, y1);
    ctx.stroke();

    // Series lines + peak markers.
    for (const s of this.series) {
      if (!s.points.length) continue;
      ctx.strokeStyle = s.color;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      let peak = s.points[0];
      for (let i = 0; i < s.points.length; i++) {
        const p = s.points[i];
        const px = xAt(p.t);
        const py = yAt(p.v);
        if (i === 0) ctx.moveTo(px, py);
        else ctx.lineTo(px, py);
        if (p.v > peak.v) peak = p;
      }
      ctx.stroke();
      // Peak marker dot.
      ctx.fillStyle = s.color;
      ctx.beginPath();
      ctx.arc(xAt(peak.t), yAt(peak.v), 2.5, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  /** Drop all samples (e.g. on engine restart). */
  clear() {
    for (const s of this.series) s.points.length = 0;
  }
}

// Classic-script global so dashboard.js (also a classic script) can reach it
// via bare-name lookup, mirroring the WsClient pattern in ws_client.js.
if (typeof window !== "undefined") window.MiniChart = MiniChart;
