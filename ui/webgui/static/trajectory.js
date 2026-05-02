/**
 * trajectory.js — WebGUI Trajectory control panel handlers
 * Phase B2: shape / speed / radius / elevation sliders + Start / Stop / Refresh
 */

(function () {
  "use strict";

  const POLL_INTERVAL_MS = 5000;

  /* ── helpers ──────────────────────────────────────────────────────────── */

  function getFloat(id) {
    return parseFloat(document.getElementById(id).value);
  }
  function getInt(id) {
    return parseInt(document.getElementById(id).value, 10);
  }
  function getString(id) {
    return document.getElementById(id).value;
  }

  function updateSliderLabel(sliderId, labelId, decimals) {
    const slider = document.getElementById(sliderId);
    const label  = document.getElementById(labelId);
    if (!slider || !label) return;
    label.textContent = parseFloat(slider.value).toFixed(decimals);
    slider.addEventListener("input", () => {
      label.textContent = parseFloat(slider.value).toFixed(decimals);
    });
  }

  /* ── API calls ────────────────────────────────────────────────────────── */

  async function startTrajectory() {
    const body = {
      obj_id:        getInt("traj-obj-id"),
      shape:         getString("traj-shape"),
      speed_hz:      getFloat("traj-speed"),
      radius:        getFloat("traj-radius"),
      elevation_rad: getFloat("traj-elevation"),
      az_start_rad:  0.0,
      az_end_rad:    Math.PI,
      lissajous_ratio: 2.0,
    };
    try {
      const res = await fetch("/api/trajectory/start", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      if (!res.ok) {
        const txt = await res.text();
        console.error("[traj] start failed:", res.status, txt);
        setTrajStatus("오류: " + res.status, "#e74c3c");
        return;
      }
      setTrajStatus("실행 중 (obj " + body.obj_id + ")", "#2ecc71");
      refreshList();
    } catch (err) {
      console.error("[traj] start error:", err);
      setTrajStatus("연결 오류", "#e74c3c");
    }
  }

  async function stopTrajectory() {
    const objId = getInt("traj-obj-id");
    try {
      const res = await fetch("/api/trajectory/stop", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ obj_id: objId }),
      });
      if (!res.ok) {
        const txt = await res.text();
        console.error("[traj] stop failed:", res.status, txt);
        setTrajStatus("정지 오류: " + res.status, "#e74c3c");
        return;
      }
      setTrajStatus("정지됨", "#888");
      refreshList();
    } catch (err) {
      console.error("[traj] stop error:", err);
      setTrajStatus("연결 오류", "#e74c3c");
    }
  }

  async function refreshList() {
    try {
      const res = await fetch("/api/trajectory/list");
      if (!res.ok) return;
      const configs = await res.json();
      const ul = document.getElementById("trajectory-list");
      if (!ul) return;
      ul.innerHTML = "";
      if (configs.length === 0) {
        const li = document.createElement("li");
        li.textContent = "활성 trajectory 없음";
        li.style.color = "#555";
        ul.appendChild(li);
        return;
      }
      configs.forEach((c) => {
        const li = document.createElement("li");
        li.textContent =
          "obj " + c.obj_id +
          " | " + c.shape +
          " | " + c.speed_hz.toFixed(2) + " Hz" +
          " | r=" + c.radius.toFixed(2) +
          " | el=" + c.elevation_rad.toFixed(2) + " rad";
        ul.appendChild(li);
      });
    } catch (err) {
      console.error("[traj] list error:", err);
    }
  }

  function setTrajStatus(text, color) {
    const el = document.getElementById("traj-status");
    if (!el) return;
    el.textContent = text;
    el.style.color = color || "#ccc";
  }

  /* ── init ─────────────────────────────────────────────────────────────── */

  function init() {
    updateSliderLabel("traj-speed",     "traj-speed-val",     2);
    updateSliderLabel("traj-radius",    "traj-radius-val",    2);
    updateSliderLabel("traj-elevation", "traj-elevation-val", 3);

    const btnStart = document.getElementById("traj-btn-start");
    const btnStop  = document.getElementById("traj-btn-stop");
    const btnRefresh = document.getElementById("traj-btn-refresh");

    if (btnStart)   btnStart.addEventListener("click", startTrajectory);
    if (btnStop)    btnStop.addEventListener("click", stopTrajectory);
    if (btnRefresh) btnRefresh.addEventListener("click", refreshList);

    // Initial list load + polling
    refreshList();
    setInterval(refreshList, POLL_INTERVAL_MS);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
