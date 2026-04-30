// core/src/core/Constants.h — pinned engine-wide constants.
//
// All real-time-path tunables live here. Changes require an ADR amendment.

#pragma once

namespace spe {

// Object slot count. Spec mandates 8 simultaneous; we reserve 2x headroom
// to allow algorithm-swap parallel-run (ADR 0006) without growth.
inline constexpr int MAX_OBJECTS = 16;

// Hard upper bound on per-callback block size. JUCE host refuses to start
// if the device reports a larger buffer (P1 startup gate).
inline constexpr int MAX_BLOCK = 512;

// Algorithm-swap crossfade window in samples (~5.33 ms @ 48 kHz).
// ADR 0006: chosen middle between K=64 (too short) and K=512 (2x CPU spike).
inline constexpr int ALGO_SWAP_K = 256;

// Speed of sound at 20 °C, in m/s. Used by PropagationDelay and WFS.
inline constexpr float SOUND_C = 343.0f;

// Heartbeat publish rate (Hz) and miss threshold (consecutive missed beats).
// C6 corrected: 10 Hz / 300 ms gate replaces the old 30 Hz / 10 s alarm.
inline constexpr int HEARTBEAT_HZ = 10;
inline constexpr int HEARTBEAT_MISS_THRESHOLD = 3;

// IPC schema version on the wire (u16 first arg of every command).
inline constexpr unsigned int SCHEMA_VERSION = 1;

}  // namespace spe
