/**
 * ws_client.js — WebSocket client with exponential back-off reconnect.
 * Parses server messages and notifies registered listeners.
 */

const WsClient = (() => {
  const listeners = [];
  let ws = null;
  let retryDelay = 500;   // ms, doubles on each failure (cap 30 s)
  const MAX_DELAY = 30000;
  let reconnectTimer = null;

  function getWsUrl() {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    return `${proto}://${location.host}/ws`;
  }

  function connect() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return;

    ws = new WebSocket(getWsUrl());

    ws.onopen = () => {
      console.log("[WsClient] connected");
      retryDelay = 500;  // reset back-off
    };

    ws.onmessage = (evt) => {
      let data;
      try {
        data = JSON.parse(evt.data);
      } catch (e) {
        console.warn("[WsClient] non-JSON message:", evt.data);
        return;
      }
      listeners.forEach((fn) => {
        try { fn(data); } catch (e) { console.error("[WsClient] listener error", e); }
      });
    };

    ws.onerror = (e) => {
      console.warn("[WsClient] error", e);
    };

    ws.onclose = () => {
      console.log(`[WsClient] closed — retry in ${retryDelay}ms`);
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

  /** Send a typed message to the server. */
  function send(payload) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      console.warn("[WsClient] not connected, drop:", payload);
      return;
    }
    ws.send(JSON.stringify(payload));
  }

  /** Register a callback for incoming server messages. */
  function onMessage(fn) {
    listeners.push(fn);
  }

  // Start connection immediately when script loads.
  connect();

  return { send, onMessage, connect };
})();
