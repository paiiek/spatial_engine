// === PORTED (Dreamscape Convergence) ========================================
// Source: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   commit f2cb796 . Source/SpeakerKind.h . Direct port authorized (convergence D3).
// Adaptations (mechanical, not logic):
//   - Wrapped in namespace iae (upstream was global) to avoid polluting the
//     mmhoa global namespace; the WFS kernel references it within iae.
// Frame-agnostic: this is a pure speaker-role tag, no geometry.
// ============================================================================
#pragma once

namespace iae
{

/** 스피커 역할: 공간 패닝 그룹 vs 보조 출력 (향후 서브믹스 분리용). */
enum class SpeakerKind : int
{
    Frontal = 0,
    Surround,
    Height,
    SpatialSubwoofer,
    Aux,
    kSpeakerKindCount
};

} // namespace iae
