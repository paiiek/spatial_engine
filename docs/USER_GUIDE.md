# Spatial Engine — User Guide

Quick reference for DAW users. Engineering deep-dives live in
`docs/architecture.md`, ADRs in `docs/adr/`, and per-area manuals in
`docs/manual_kr/`.

## Binaural Bus (Bus 1)

The plugin exposes two output buses:

- **Bus 0 — Speakers** — multichannel renderer output.
- **Bus 1 — Binaural (stereo)** — HRTF render for headphone monitoring.

### Bus 1 fallback policy

If no SOFA is loaded while the plugin is active, bus 1 is silent and
`/sys/binaural_warning ,s no_sofa_loaded` is emitted once per prepare cycle.
**Some DAWs (Logic, Cubase) auto-collapse silent stereo tracks** — keep an
OSC subscriber on `/sys/binaural_warning` to detect the configuration
error. Under bypass, bus 1 reverts to the -6 dB diagnostic downmix of bus 0
channels 0+1 so users still hear a recognisable signal.

The current fallback state is also surfaced on `/sys/state ,s
"fallback_mode=muted"` (or `"fallback_mode=normal"`) once per prepare
cycle.

### Wire surface

```
/sys/binaural_warning ,s "no_sofa_loaded"      # one-shot per prepareToPlay, active path only
/sys/binaural_warning ,sf "ambivs_disabled_cpu" <throughput>   # B2→B1 fallback
/sys/binaural_warning ,s "xfade_truncated_cpu" # mode-transition crossfade truncated
/sys/binaural_status  ,i  <load_into_failures> # 1 Hz heartbeat
/sys/state            ,s  "fallback_mode=<muted|normal>"      # once per prepareToPlay
```

## Network exposure (OSC bind address)

The standalone engine's OSC listener binds to **127.0.0.1 (loopback only)** by
default. The OSC command surface — including `/sys/load_layout`,
`/sys/binaural_sofa` (filesystem path arg), and the per-object ADM-OSC tags —
is **unauthenticated**. Loopback-only default means an attacker on the same
LAN cannot drive arbitrary layout / SOFA loads against your engine without
first compromising the host.

### Cross-machine deployments

Pass `--osc-bind <addr>` on the standalone CLI to bind a specific interface:

```bash
# Default (safe single-host IPC):
spatial_engine_core --osc-port 9100

# Bind every interface — cross-machine production. ENGINE BECOMES
# REACHABLE FROM THE LAN. Use only on a trusted network behind a firewall
# that filters the OSC port.
spatial_engine_core --osc-port 9100 --osc-bind 0.0.0.0

# Bind a specific NIC:
spatial_engine_core --osc-port 9100 --osc-bind 192.168.10.5
```

The engine prints a `WARNING: OSC listener bound to <addr>. Engine is
reachable from LAN; OSC commands are unauthenticated.` line on stderr when
the bind address is not `127.0.0.1`.

### Recommendations for multi-NIC hosts

- Pick a single NIC via `--osc-bind <NIC_ip>` rather than `0.0.0.0`.
- Firewall the OSC UDP port (`--osc-port`) so only the control workstation
  can reach it.
- Keep loopback-only telemetry consumers (binaural warning, status
  heartbeat) on `127.0.0.1`. The reply-port override gate (`overridePeerPort`)
  already requires the peer to be loopback before honouring a handshake's
  `reply_port`, so the bus-1 silence indicator
  (`/sys/binaural_warning ,s "no_sofa_loaded"`) and the
  `/sys/state` fallback line cannot be redirected to a third-party victim
  even on a `--osc-bind 0.0.0.0` deployment.

See also the bus-1 silence note above and the Q3 `/sys/state` fallback
emission for telemetry subscribers that depend on the loopback-only model.
