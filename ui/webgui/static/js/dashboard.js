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

  // --- Control-plane socket (/ws) for outbound commands (A-M5) -------------
  // /ws/metrics is push-only; control commands (e.g. binaural_reset_demote)
  // must go over the /ws control plane. Minimal own connection mirroring the
  // ws_client.js exponential-back-off idiom (dashboard.html does NOT load the
  // main-UI ws_client.js).
  let ctrlWs = null;
  let ctrlRetryDelay = 500;
  let ctrlReconnectTimer = null;

  function ctrlWsUrl() {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    return `${proto}://${location.host}/ws`;
  }

  function ctrlConnect() {
    if (ctrlWs && (ctrlWs.readyState === WebSocket.OPEN || ctrlWs.readyState === WebSocket.CONNECTING)) return;
    ctrlWs = new WebSocket(ctrlWsUrl());
    ctrlWs.onopen = () => { ctrlRetryDelay = 500; };
    ctrlWs.onerror = (e) => console.warn("[dashboard] ctrl ws error", e);
    ctrlWs.onclose = () => {
      if (ctrlReconnectTimer) return;
      ctrlReconnectTimer = setTimeout(() => {
        ctrlReconnectTimer = null;
        ctrlRetryDelay = Math.min(ctrlRetryDelay * 2, MAX_DELAY);
        ctrlConnect();
      }, ctrlRetryDelay);
    };
  }

  function ctrlSend(payload) {
    if (!ctrlWs || ctrlWs.readyState !== WebSocket.OPEN) {
      console.warn("[dashboard] ctrl ws not connected, drop:", payload);
      return;
    }
    ctrlWs.send(JSON.stringify(payload));
  }

  function onResetDemote() {
    ctrlSend({ type: "binaural_reset_demote" });
  }

  // --- Reset-demote button (DOM placed in A-M4; behavior wired in A-M5) -----
  function wireResetButton() {
    const btn = el("btn-reset-demote");
    if (!btn) return;
    btn.addEventListener("click", onResetDemote);
  }

  // --- HRTF dataset selector (B-M5) ----------------------------------------
  function onSofaSelect(name) {
    ctrlSend({ type: "binaural_sofa_select", name: name });
  }

  function wireSofaSelect() {
    const sel = el("binaural-sofa-select");
    if (!sel) return;
    // Populate from /api/hrtf/catalog, then wire the onchange handler.
    fetch("/api/hrtf/catalog")
      .then(function (r) { return r.json(); })
      .then(function (data) {
        const entries = data.hrtf_catalog || [];
        sel.innerHTML = "";
        entries.forEach(function (entry) {
          const opt = document.createElement("option");
          opt.value = entry.name;
          opt.textContent = entry.display_name || entry.name;
          sel.appendChild(opt);
        });
        if (entries.length === 0) {
          const opt = document.createElement("option");
          opt.value = "";
          opt.textContent = "(no catalog)";
          sel.appendChild(opt);
        }
      })
      .catch(function () {
        sel.innerHTML = "<option value=''>(catalog unavailable)</option>";
      });
    sel.addEventListener("change", function () {
      if (sel.value) onSofaSelect(sel.value);
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
    wireSofaSelect();
    wireScenePanel();
    wireCuePanel();
    loadScenes();
    loadCues();
    connect();
    ctrlConnect();
  }

  // --- E-M4 — Scene library (§6) ------------------------------------------

  function loadScenes() {
    fetch("/api/scenes")
      .then(function (r) { return r.json(); })
      .then(function (data) {
        renderSceneList(data.scenes || []);
      })
      .catch(function () {
        const ul = el("scene-list");
        if (ul) ul.innerHTML = "<li style='color:#f99'>fetch error</li>";
      });
  }

  function renderSceneList(scenes) {
    const ul = el("scene-list");
    if (!ul) return;
    if (scenes.length === 0) {
      ul.innerHTML = "<li style='color:#555'>(no scenes)</li>";
      return;
    }
    ul.innerHTML = "";
    scenes.forEach(function (s) {
      const name = s.name || String(s);
      const li = document.createElement("li");
      const span = document.createElement("span");
      span.className = "scene-name";
      span.textContent = name;
      span.title = name;

      const btnLoad = document.createElement("button");
      btnLoad.className = "scene-btn";
      btnLoad.textContent = "Load";
      btnLoad.addEventListener("click", function () {
        ctrlSend({ type: "scene_load", name: name });
      });

      const btnRename = document.createElement("button");
      btnRename.className = "scene-btn";
      btnRename.textContent = "Rename";
      btnRename.addEventListener("click", function () {
        const to = window.prompt("Rename '" + name + "' to:", name);
        if (to && to !== name) {
          ctrlSend({ type: "scene_rename", from: name, to: to });
          setTimeout(loadScenes, 300);
        }
      });

      const btnDup = document.createElement("button");
      btnDup.className = "scene-btn";
      btnDup.textContent = "Dup";
      btnDup.addEventListener("click", function () {
        const to = window.prompt("Duplicate '" + name + "' as:", name + "_copy");
        if (to) {
          ctrlSend({ type: "scene_duplicate", from: name, to: to });
          setTimeout(loadScenes, 300);
        }
      });

      const btnDel = document.createElement("button");
      btnDel.className = "scene-btn danger";
      btnDel.textContent = "Del";
      btnDel.addEventListener("click", function () {
        if (window.confirm("Delete scene '" + name + "'?")) {
          ctrlSend({ type: "scene_delete", name: name });
          setTimeout(loadScenes, 300);
        }
      });

      li.appendChild(span);
      li.appendChild(btnLoad);
      li.appendChild(btnRename);
      li.appendChild(btnDup);
      li.appendChild(btnDel);
      ul.appendChild(li);
    });
  }

  function wireScenePanel() {
    const btnSave = el("btn-scene-save");
    if (btnSave) {
      btnSave.addEventListener("click", function () {
        const input = el("scene-name-input");
        const name = (input ? input.value.trim() : "") || "scene";
        ctrlSend({ type: "scene_save", name: name });
        setTimeout(loadScenes, 300);
      });
    }
    const btnRefresh = el("btn-scene-refresh");
    if (btnRefresh) {
      btnRefresh.addEventListener("click", loadScenes);
    }
  }

  // --- E-M4 — Cue list (§7) -----------------------------------------------

  let _currentCueIndex = -1;

  function loadCues() {
    fetch("/api/cues")
      .then(function (r) { return r.json(); })
      .then(function (data) {
        renderCueList(data.cues || []);
      })
      .catch(function () {
        const ul = el("cue-list");
        if (ul) ul.innerHTML = "<li style='color:#f99'>fetch error</li>";
      });
  }

  function renderCueList(cues) {
    const ul = el("cue-list");
    if (!ul) return;
    if (cues.length === 0) {
      ul.innerHTML = "<li style='color:#555'>(no cues)</li>";
      return;
    }
    ul.innerHTML = "";
    cues.forEach(function (c, idx) {
      const sceneName = (c && c.scene_name) ? c.scene_name : String(c);
      const li = document.createElement("li");
      if (idx === _currentCueIndex) li.classList.add("active-cue");

      const idxSpan = document.createElement("span");
      idxSpan.className = "cue-idx";
      idxSpan.textContent = String(idx);

      const nameSpan = document.createElement("span");
      nameSpan.className = "cue-scene";
      nameSpan.textContent = sceneName;
      nameSpan.title = sceneName;

      const btnGo = document.createElement("button");
      btnGo.className = "scene-btn";
      btnGo.textContent = "Go";
      btnGo.addEventListener("click", function () {
        ctrlSend({ type: "cue_go", index: idx });
        _currentCueIndex = idx;
        renderCueList(cues);
      });

      li.appendChild(idxSpan);
      li.appendChild(nameSpan);
      li.appendChild(btnGo);
      ul.appendChild(li);
    });
  }

  function wireCuePanel() {
    const btnPrev = el("btn-cue-prev");
    if (btnPrev) btnPrev.addEventListener("click", function () { ctrlSend({ type: "cue_prev" }); });

    const btnNext = el("btn-cue-next");
    if (btnNext) btnNext.addEventListener("click", function () { ctrlSend({ type: "cue_next" }); });

    const btnStop = el("btn-cue-stop");
    if (btnStop) btnStop.addEventListener("click", function () { ctrlSend({ type: "cue_stop" }); });

    const btnRefresh = el("btn-cue-refresh");
    if (btnRefresh) btnRefresh.addEventListener("click", loadCues);
  }

  // Expose a small surface for the playwright smoke test (and A-M5): inject a
  // mock message and observe a draw, without a live engine.
  window.__dashboard = {
    handleMessage,
    connect,
    onResetDemote,
    onSofaSelect,
    // Expose ctrlSend so the playwright smoke can spy on outbound /ws sends
    // without a live engine.
    ctrlSend: (payload) => ctrlSend(payload),
    // E-M4: expose scene/cue loaders for playwright smoke test.
    loadScenes,
    loadCues,
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
