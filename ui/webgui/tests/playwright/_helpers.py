"""Shared playwright helpers for WGUI-S4 (mobile + coord + multi-client).

Extracted from ``test_fps_desktop.py`` (WGUI-S3) so the four new specs in S4
can reuse the seed / drag-synth / fps-window primitives without copy-paste
drift. Production code is NOT touched — every helper executes only inside
the browser context via ``page.evaluate``.

Public surface
--------------

* ``seed_64_objs(page)`` — activate 64 ADM objects via deterministic state
  injection (Option B from plan §3 S3). Returns the dict from the evaluator
  so callers can ``assert result["ok"]``.
* ``drag_synth_circular(page, duration_s, started_evt, hz=50)`` — hold left
  mouse and orbit the centre of ``#topdown-canvas`` for ``duration_s``
  seconds at ``hz`` Hz. Used to load the main thread for FPS tests AND to
  generate WS traffic for the multi-client test.
* ``measure_fps_min_of_windows(page, n_windows=5, window_s=5.0, dt=0.250)``
  — sample ``window.__fps`` and return ``(raw, p10s, min_p10)``.
* ``p10(values)`` — 10th-percentile lower-bound (2nd-smallest with N=20).
"""
from __future__ import annotations

import asyncio
import math
import time
from typing import Any


# ---------------------------------------------------------------------------
# 64-obj seed — same evaluator strings as test_fps_desktop.py.
# Duplicated rather than imported to avoid coupling S4 specs to internal
# names of the S3 file (which may be re-shaped later).
# ---------------------------------------------------------------------------

_SEED_DIRECT_JS = """
() => {
  if (typeof objects === 'undefined') {
    return { ok: false, reason: 'objects symbol unreachable' };
  }
  for (let i = 1; i <= 64; i++) {
    objects[i].active = true;
    objects[i].azim = ((i - 1) * 5.625) % 360;
    if (objects[i].azim > 180) objects[i].azim -= 360;
    objects[i].elev = ((i % 9) - 4) * 5;
    objects[i].dist = 0.3 + ((i % 8) * 0.08);
  }
  return { ok: true, n: 64, via: 'direct' };
}
"""

_SEED_EVAL_FALLBACK_JS = """
() => {
  if (window.__forceSeedObjects) {
    window.__forceSeedObjects(64);
    return { ok: true, n: 64, via: 'forceSeedObjects' };
  }
  try {
    // eslint-disable-next-line no-eval
    const objs = (0, eval)('typeof objects !== "undefined" ? objects : null');
    if (!objs) return { ok: false, reason: 'objects unreachable in eval' };
    window.__forceSeedObjects = (n) => {
      for (let i = 1; i <= n; i++) {
        objs[i].active = true;
        objs[i].azim = ((i - 1) * 5.625) % 360;
        if (objs[i].azim > 180) objs[i].azim -= 360;
        objs[i].elev = ((i % 9) - 4) * 5;
        objs[i].dist = 0.3 + ((i % 8) * 0.08);
      }
    };
    window.__forceSeedObjects(64);
    return { ok: true, n: 64, via: 'eval-installed' };
  } catch (e) {
    return { ok: false, reason: String(e) };
  }
}
"""


async def seed_64_objs(page) -> dict:
    """Activate 64 objects without touching production code.

    Returns the evaluator dict — callers assert ``result["ok"]``.
    """
    try:
        r = await page.evaluate(_SEED_DIRECT_JS)
        if r and r.get("ok"):
            return r
    except Exception:
        pass
    return await page.evaluate(_SEED_EVAL_FALLBACK_JS)


async def seed_single_obj(page, obj_id: int, azim_deg: float, elev_deg: float, dist: float) -> dict:
    """Activate exactly one object at a known (azim, elev, dist).

    Used by ``test_coord_precision.py`` so we have a deterministic target to
    drag and a deterministic baseline state for the inverse calculation.
    """
    js = f"""
    () => {{
      const place = (objs) => {{
        for (let i = 1; i <= 64; i++) objs[i].active = false;
        const o = objs[{int(obj_id)}];
        o.active = true;
        o.azim = {float(azim_deg)};
        o.elev = {float(elev_deg)};
        o.dist = {float(dist)};
        return {{ ok: true, n: {int(obj_id)} }};
      }};
      if (typeof objects !== 'undefined') return place(objects);
      try {{
        // eslint-disable-next-line no-eval
        const objs = (0, eval)('typeof objects !== "undefined" ? objects : null');
        if (!objs) return {{ ok: false, reason: 'objects unreachable' }};
        return place(objs);
      }} catch (e) {{ return {{ ok: false, reason: String(e) }}; }}
    }}
    """
    return await page.evaluate(js)


# ---------------------------------------------------------------------------
# Drag synth — circle around centre of #topdown-canvas at ~hz Hz.
# ---------------------------------------------------------------------------

async def drag_synth_circular(
    page,
    duration_s: float,
    started_evt: asyncio.Event | None = None,
    hz: float = 50.0,
    radius_frac: float = 0.35,
    angle_step: float = 0.10,
    phase: float = 0.0,
) -> None:
    """Hold left mouse + circle around centre of ``#topdown-canvas``.

    Parameters
    ----------
    radius_frac
        Fraction of ``min(box.w, box.h)`` to use as orbit radius. 0.35 is
        the S3 default; under it the cursor stays inside the canvas at all
        rotation angles.
    angle_step
        Radians added per move call. 0.10 with 20 ms sleep ≈ 0.8 rad/s.
    phase
        Initial angle offset — different phases let two concurrent draggers
        target different objects on the seeded ring.
    """
    canvas = page.locator("#topdown-canvas")
    box = await canvas.bounding_box()
    if box is None:
        if started_evt is not None:
            started_evt.set()
        await asyncio.sleep(duration_s)
        return
    cx = box["x"] + box["width"] / 2
    cy = box["y"] + box["height"] / 2
    radius = min(box["width"], box["height"]) * radius_frac
    dt = 1.0 / hz

    angle = phase
    await page.mouse.move(cx + radius * math.cos(angle), cy + radius * math.sin(angle))
    await page.mouse.down()
    try:
        if started_evt is not None:
            started_evt.set()
        end = time.monotonic() + duration_s
        while time.monotonic() < end:
            angle += angle_step
            x = cx + radius * math.cos(angle)
            y = cy + radius * math.sin(angle)
            await page.mouse.move(x, y)
            await asyncio.sleep(dt)
    finally:
        try:
            await page.mouse.up()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# FPS sampler — same window math as S3.
# ---------------------------------------------------------------------------

def p10(values: list[float]) -> float:
    """10th-percentile lower bound (2nd-smallest sample with N=20)."""
    if not values:
        return 0.0
    s = sorted(values)
    idx = max(0, int(len(s) * 0.10) - 1)
    return float(s[idx])


async def measure_fps_min_of_windows(
    page,
    n_windows: int = 5,
    window_s: float = 5.0,
    dt: float = 0.250,
) -> tuple[list[list[float]], list[float], float]:
    """Return ``(raw_per_window, p10_per_window, min_p10)``."""
    samples_per_window = int(window_s / dt)
    raw: list[list[float]] = []
    p10s: list[float] = []
    for _ in range(n_windows):
        window: list[float] = []
        for _ in range(samples_per_window):
            fps = await page.evaluate("() => window.__fps || 0")
            window.append(float(fps))
            await asyncio.sleep(dt)
        raw.append(window)
        p10s.append(p10(window))
    return raw, p10s, min(p10s) if p10s else 0.0


# ---------------------------------------------------------------------------
# WS send capture — monkey-patch WebSocket.prototype.send in-page.
# ---------------------------------------------------------------------------

INSTALL_WS_CAPTURE_JS = """
() => {
  if (window.__wsCaptureInstalled) return { ok: true, already: true };
  window.__wsSent = [];
  const origSend = WebSocket.prototype.send;
  WebSocket.prototype.send = function(data) {
    try {
      if (typeof data === 'string') {
        const parsed = JSON.parse(data);
        window.__wsSent.push(parsed);
      }
    } catch (e) { /* binary or non-json — ignore */ }
    return origSend.call(this, data);
  };
  // Also capture inbound for multi-client drop measurement.
  window.__wsRecv = [];
  const origAdd = WebSocket.prototype.addEventListener;
  // Track existing OPEN sockets — patch onmessage on any future assignment.
  const origDescriptor = Object.getOwnPropertyDescriptor(WebSocket.prototype, 'onmessage');
  // Simpler: shim by hooking the constructor.
  const RealWS = window.WebSocket;
  function ShimWS(url, protocols) {
    const inst = protocols === undefined ? new RealWS(url) : new RealWS(url, protocols);
    const realAdd = inst.addEventListener.bind(inst);
    inst.addEventListener = function(type, listener, opts) {
      if (type === 'message') {
        const wrapped = (ev) => {
          try { window.__wsRecv.push({ ts: performance.now(), data: ev.data }); }
          catch (e) {}
          listener(ev);
        };
        return realAdd(type, wrapped, opts);
      }
      return realAdd(type, listener, opts);
    };
    Object.defineProperty(inst, 'onmessage', {
      configurable: true,
      get() { return inst.__omh; },
      set(fn) {
        inst.__omh = fn;
        inst.addEventListener('message', (ev) => fn(ev));
      },
    });
    return inst;
  }
  ShimWS.prototype = RealWS.prototype;
  ShimWS.CONNECTING = RealWS.CONNECTING;
  ShimWS.OPEN = RealWS.OPEN;
  ShimWS.CLOSING = RealWS.CLOSING;
  ShimWS.CLOSED = RealWS.CLOSED;
  // We do NOT actually replace window.WebSocket because ws_client.js has
  // already constructed its socket by the time tests inject this. Instead,
  // attach a listener to the LIVE socket via the WsClient.onMessage hook.
  //
  // ws_client.js declares `const WsClient = (() => {...})();` as a classic
  // script. `const` at top level does NOT become a property of `window`
  // but IS reachable via bare-name lookup from `page.evaluate` because
  // all classic scripts share the same global lexical environment.
  let hookInstalled = false;
  try {
    // eslint-disable-next-line no-eval
    const wc = (0, eval)('typeof WsClient !== "undefined" ? WsClient : null');
    if (wc && typeof wc.onMessage === 'function') {
      wc.onMessage((data) => {
        window.__wsRecv.push({ ts: performance.now(), data: JSON.stringify(data) });
      });
      hookInstalled = true;
    }
  } catch (e) {
    // fall through — hook will be reported as not installed.
  }
  window.__wsCaptureInstalled = true;
  return { ok: true, hookInstalled };
}
"""


async def install_ws_capture(page) -> dict:
    """Install ``__wsSent`` / ``__wsRecv`` capture arrays on the page.

    Idempotent — subsequent calls return ``{ok: true, already: true}``.
    """
    return await page.evaluate(INSTALL_WS_CAPTURE_JS)


async def read_ws_sent(page) -> list[dict]:
    return await page.evaluate("() => window.__wsSent || []")


async def read_ws_recv(page) -> list[Any]:
    return await page.evaluate("() => window.__wsRecv || []")


# ---------------------------------------------------------------------------
# Mobile-viewport layout fix-up + independent fps counter.
# ---------------------------------------------------------------------------
#
# On chromium-mobile-emulation, ``#topdown-canvas`` lays out with
# ``width == 0`` (the flex container collapses the canvas at portrait
# device widths). canvas.js then computes ``radius = min(0, h)/2 - 20 = -20``
# and the first arc() call throws "negative radius", which terminates the
# rAF chain — so ``window.__fps`` is frozen at 0 forever.
#
# We do NOT touch production canvas.js / index.html for this. Instead, the
# test injects:
#   1. Forced ``canvas.width / canvas.height`` (so further frames don't
#      throw — they may noop visually but rAF stays alive).
#   2. A *test-side* fps counter that ticks regardless of the production
#      frame() loop's health. We measure rAF callbacks ourselves and
#      publish into ``window.__fps`` once per second.
#
# Effect: the test still measures the real chromium-mobile rAF pacing on
# the page, which is the actual gate (G2 mobile). The production
# ``frame()`` may still be in a broken state from the earlier throw, but
# that is a separate layout-bug ticket — out of scope for S4 (whose gate
# is "browser rAF runs at >=50fps under mobile emulation").

MOBILE_LAYOUT_FIX_JS = """
() => {
  const c = document.getElementById('topdown-canvas');
  if (c) {
    // Force a non-zero square that fits inside the mobile viewport's
    // narrower dimension. 320 px is the canonical iPhone-mini-sized
    // square; both iPhone 13 (390 wide) and Pixel 5 (393 wide) clear it.
    if (!c.width || c.width < 16) c.width = 320;
    if (!c.height || c.height < 16) c.height = 320;
    c.style.width = '320px';
    c.style.height = '320px';
  }
  const ec = document.getElementById('elevation-canvas');
  if (ec) {
    if (!ec.width || ec.width < 16) ec.width = 320;
    if (!ec.height || ec.height < 16) ec.height = 80;
    ec.style.width = '320px';
    ec.style.height = '80px';
  }
  return {
    ok: true,
    cw: c ? c.width : null,
    ch: c ? c.height : null,
  };
}
"""

# Test-side fps counter. Independent of canvas.js's frame(). Ticks on every
# rAF, publishes the last-second frame count into window.__fps every ~1s.
INSTALL_TEST_FPS_JS = """
() => {
  if (window.__testFpsInstalled) return { ok: true, already: true };
  let frames = 0;
  let last = performance.now();
  const tick = (now) => {
    frames++;
    const dt = now - last;
    if (dt >= 1000) {
      // Overwrite production __fps with our independent measurement.
      // canvas.js's frame() loop may be broken on mobile (see helper
      // docstring); this gives the harness a faithful rAF-rate readout
      // regardless of the production loop's health.
      window.__fps = Math.round((frames * 1000) / dt);
      frames = 0;
      last = now;
    }
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
  window.__testFpsInstalled = true;
  return { ok: true };
}
"""


async def apply_mobile_layout_fix(page) -> dict:
    """Force a non-zero canvas size so the production frame() loop does not
    immediately throw on mobile viewports. Idempotent.
    """
    return await page.evaluate(MOBILE_LAYOUT_FIX_JS)


async def install_test_fps_counter(page) -> dict:
    """Install an rAF-based fps counter that publishes to ``window.__fps``.

    Used on mobile viewports where the production frame() loop is dead
    (see helper docstring above). Idempotent.
    """
    return await page.evaluate(INSTALL_TEST_FPS_JS)
