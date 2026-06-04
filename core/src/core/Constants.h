// core/src/core/Constants.h — pinned engine-wide constants.
//
// All real-time-path tunables live here. Changes require an ADR amendment.

#pragma once

namespace spe {

// Object slot count. Spec mandates 8 simultaneous; expanded to 64 for
// larger venue deployments (US-002). Allows algorithm-swap parallel-run
// (ADR 0006) without growth.
//
// v0.9 Lane C (ADR amendment): the cap is now a compile-time option driven by
// the cmake cache var SPATIAL_ENGINE_MAX_OBJECTS ∈ {64,128}, which defines the
// SPE_MAX_OBJECTS macro. Default stays 64 — a bare build is byte-identical.
// All object-dimension caps (scene::MAX_OBJECTS, STATE_MAX_OBJECTS,
// kEchoMaxObjects) derive from this single source of truth.
#ifndef SPE_MAX_OBJECTS
#define SPE_MAX_OBJECTS 64
#endif
inline constexpr int MAX_OBJECTS = SPE_MAX_OBJECTS;

// Speaker (output-channel) slot count. Phase 0.5 (128 lift): mirrors the
// MAX_OBJECTS option exactly — a compile-time cap driven by the cmake cache var
// SPATIAL_ENGINE_MAX_SPEAKERS ∈ {64,128}, which defines the SPE_MAX_SPEAKERS
// macro. Default stays 64 so a bare build is byte-identical. Every
// speaker-dimension scratch (AlgoScratch.gains, the renderers' ramps_/position
// buffers, AlgorithmAnalyticReference::kMaxVbapSpeakers, WFSRenderer::
// MAX_SPEAKERS, SpeakerLayout::kMaxYamlChannel) derives from this single source
// of truth. The ported reference buffers are already sized 128
// (ported/SpatialMath.h kPrototypeChannels), so the 128 ceiling is safe.
#ifndef SPE_MAX_SPEAKERS
#define SPE_MAX_SPEAKERS 64
#endif
inline constexpr int MAX_SPEAKERS = SPE_MAX_SPEAKERS;

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
