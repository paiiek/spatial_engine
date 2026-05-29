/**
 * dashboard.js — real-time metrics dashboard client (v0.9 Lane A · A-M4).
 *
 * Connects to the dedicated ``/ws/metrics`` channel (separate from the
 * positional ``/ws`` control plane), maintains a 60-second ring buffer per
 * series, and drives self-hosted canvas minicharts (minichart.js — DD-C
 * LOCKED, zero external JS/CDN). Reuses the exponential-back-off reconnect
 * idiom from ws_client.js.
 *
 * Message shapes received on /ws/metrics (classified by osc_bridge.py A-M2):
 *   metrics → { type:"metrics", cpu_pct, cpu_peak_pct, p99_us, xrun_count,
 *               engine_overrun_count, binaural_demote_count }
 *   warning → { type:"warning", ts, category, payload }
 *
 * On connect the server pushes the latest-wins snapshot immediately
 * (MetricsHub, A-M3), which may be a single metrics OR warning message.
 *
 * A9-Q8: cpu_peak_pct is currently clamped to [0,100] engine-side. It is
 * rendered as its own line on the CPU chart; we do NOT assume it exceeds 100.
 */

(() => {
  "use strict";

  const MAX_WARNINGS = 50;
  const WINDOW_SEC = 60;

  // --- Reconnecting WebSocket to /ws/metrics (ws_client.js idiom) ----------
  let ws = null;
  let retryDelay = 500;
  const MAX_DELAY = 30000;
  let reconnectTimer = null;

  function metricsWsUrl() {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    return `${proto}://${location.host}/ws/metrics`;
  }

  function connect() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return;
    ws = new WebSocket(metricsWsUrl());
    ws.onopen = () => {
      retryDelay = 500;
      setConn(true);
    };
    ws.onmessage = (evt) => {
      let data;
      try {
        data = JSON.parse(evt.data);
      } catch (e) {
        console.warn("[dashboard] non-JSON message:", evt.data);
        return;
      }
      handleMessage(data);
    };
    ws.onerror = (e) => console.warn("[dashboard] ws error", e);
    ws.onclose = () => {
      setConn(false);
      scheduleReconnect();
    };
  }

  function scheduleReconnect() {
    if (reconnectTimer) return;
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      retryDelay = Math.min(retryDelay * 2, MAX_DELAY);
      connect();
    }, retryDelay);
  }

  // --- DOM handles ---------------------------------------------------------
  const el = (id) => document.getElementById(id);
  let connDot, connLabel;
  let cpuChart, xrunChart, p99Chart;
  let warningListEl;
  let lastXrun = null;

  function setConn(up) {
    if (!connDot) return;
    connDot.classList.toggle("connected", !!up);
    if (connLabel) connLabel.textContent = up ? "connected" : "disconnected";
  }

  // --- Message handling ----------------------------------------------------
  function handleMessage(data) {
    if (!data || typeof data !== "object") return;
    if (data.type === "metrics") {
      applyMetrics(data);
    } else if (data.type === "warning") {
      appendWarning(data);
    }
    // Unknown types are ignored — /ws/metrics only carries metrics/warning.
  }

  function num(v) {
    const n = Number(v);
    return Number.isFinite(n) ? n : 0;
  }

  function applyMetrics(m) {
    const cpu = num(m.cpu_pct);
    const peak = num(m.cpu_peak_pct);
    const p99 = num(m.p99_us);
    const xrun = num(m.xrun_count);
    const overrun = num(m.engine_overrun_count);
    const demote = num(m.binaural_demote_count);

    // CPU chart: avg + peak lines (peak is clamped [0,100] — A9-Q8).
    if (cpuChart) cpuChart.push({ cpu, peak });
    // p99 latency chart.
    if (p99Chart) p99Chart.push({ p99 });
    // xrun chart: plot the per-tick DELTA so the line shows new drop-outs,
    // not an ever-rising cumulative counter. First sample seeds the baseline.
    if (xrunChart) {
      const delta = lastXrun == null ? 0 : Math.max(0, xrun - lastXrun);
      xrunChart.push({ xrun: delta });
    }
    lastXrun = xrun;

    // Top status / numeric readouts.
    setText("stat-cpu", cpu.toFixed(0) + " %");
    setText("stat-cpu-peak", peak.toFixed(0) + " %");
    setText("stat-p99", p99.toFixed(0) + " µs");
    setText("stat-xrun", String(xrun));
    setText("stat-overrun", String(overrun));

    // Binaural panel: demote flag (A-M1 emits a sticky 0/1 runtime-demote
    // flag in binaural_demote_count).
    const demoteEl = el("binaural-demote");
    if (demoteEl) {
      const active = demote > 0;
      demoteEl.textContent = active ? "DEMOTED" : "nominal";
      demoteEl.classList.toggle("demoted", active);
    }

    renderCharts();
  }

  function appendWarning(w) {
    if (!warningListEl) return;
    const li = document.createElement("li");
    const t = w.ts ? new Date(w.ts * 1000).toLocaleTimeString() : "";
    const cat = w.category != null ? String(w.category) : "(warning)";
    const pay = w.payload != null ? ` — ${w.payload}` : "";
    li.textContent = `${t}  ${cat}${pay}`;
    warningListEl.insertBefore(li, warningListEl.firstChild);
    // Keep only the last MAX_WARNINGS entries.
    while (warningListEl.children.length > MAX_WARNINGS) {
      warningListEl.removeChild(warningListEl.lastChild);
    }
  }

  function setText(id, text) {
    const e = el(id);
    if (e) e.textContent = text;
  }

  function renderCharts() {
    if (cpuChart) cpuChart.render();
    if (xrunChart) xrunChart.render();
    if (p99Chart) p99Chart.render();
  }

  // --- Reset-demote button (DOM placed in A-M4; behavior wired in A-M5) -----
  function wireResetButton() {
    const btn = el("btn-reset-demote");
    if (!btn) return;
    btn.addEventListener("click", () => {
      // A-M5 wires the WS send. Placeholder: send on the metrics socket is
      // not the control plane, so A-M5 will route via the /ws control socket.
      if (window.__dashboard && typeof window.__dashboard.onResetDemote === "function") {
        window.__dashboard.onResetDemote();
      }
    });
  }

  // --- Boot ----------------------------------------------------------------
  function init() {
    connDot = el("conn-dot");
    connLabel = el("conn-label");
    warningListEl = el("warning-log");

    const MC = window.MiniChart;
    const cpuCanvas = el("chart-cpu");
    const xrunCanvas = el("chart-xrun");
    const p99Canvas = el("chart-p99");

    if (MC && cpuCanvas) {
      cpuChart = new MC(cpuCanvas, {
        windowSec: WINDOW_SEC,
        yMin: 0,
        yMax: 100,
        yUnit: "%",
        series: [
          { key: "cpu", color: "#5cf", label: "CPU avg" },
          { key: "peak", color: "#f80", label: "CPU peak" },
        ],
      });
    }
    if (MC && xrunCanvas) {
      xrunChart = new MC(xrunCanvas, {
        windowSec: WINDOW_SEC,
        yMin: 0,
        yMax: null, // auto-scale — xrun deltas are usually 0
        series: [{ key: "xrun", color: "#e74c3c", label: "xrun Δ" }],
      });
    }
    if (MC && p99Canvas) {
      p99Chart = new MC(p99Canvas, {
        windowSec: WINDOW_SEC,
        yMin: 0,
        yMax: null,
        yUnit: "µs",
        series: [{ key: "p99", color: "#9b59b6", label: "p99" }],
      });
    }

    // Initial blank render so axes/grid are visible before the first sample.
    renderCharts();
    wireResetButton();
    connect();
  }

  // Expose a small surface for the playwright smoke test (and A-M5): inject a
  // mock message and observe a draw, without a live engine.
  window.__dashboard = {
    handleMessage,
    connect,
    get charts() {
      return { cpu: cpuChart, xrun: xrunChart, p99: p99Chart };
    },
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
