// === PORTED (Dreamscape Convergence) ========================================
// Source: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   commit f2cb796 . Source/WfsDrivingParams.h . Direct port authorized (convergence D3).
// Adaptations (mechanical, not logic):
//   - Wrapped WfsDelayReferenceMode + WfsDrivingParams in namespace iae (upstream
//     was global); the WFS kernel references them unqualified within iae.
//   - No other changes: this is a plain scalar struct + enum (already juce-free).
// ============================================================================
#pragma once

namespace iae
{

/** WFS 지연 기준 — 실시간 처리에서는 causal하게 최소 지연을 빼 상대 지연만 사용. */
enum class WfsDelayReferenceMode : int
{
    /** 가상 소스에 가장 가까운 2차원(스피커)를 t=0 기준. */
    MinimumToNearestSecondary = 0,
    /**
     * 리스너(원점)–가상 소스 직선 경로 길이 D_lv를 기준으로 한 상대 지연.
     * causal 정규화(전 채널에서 동일 샘플 빼기) 후에는 스피커 간 상대 패턴이
     * Minimum 모드와 수학적으로 동일해짐(단일 소스·자유장에서 동차 경로 차이만 의미 있음).
     */
    ListenerVirtualPath = 1,
};

/** 원론적 2.5D 유틸리티 WFS에 쓰는 스칼라 파라미터(현장 조정용). */
struct WfsDrivingParams
{
    /** 음속 (m/s). */
    float speedOfSound = 343.f;
    /**
     * 진폭 거리 법칙: gain ∝ 1 / d^exponent.
     * 1.f ≈ 단순 기하 감쇠(3D Green 근사), 0.5f ≈ 1/√d (2.5D 면적 보존에 가까운 스케일).
     */
    float amplitudeDistanceExponent = 1.f;
    /**
     * 난반사(cos φ): 오리엔테이션 벡터 vs 원점 방향(청음 영역 쪽) 블렌드.
     * 0 = 오리엔테이션만, 1 = 스피커→원점 방향만(배열 바깥 소스에서 좌우 반전 완화).
     */
    float obliquityRadialBlend = 0.72f;
    /**
     * VBAP 진폭 벡터와 WFS 진폭을 선형 혼합 후 Σg²=1 재정규화.
     * 0 = WFS만(기존 동작), 1 = 진폭은 VBAP에 가깝게, 지연은 (1−값)×WFS 지연.
     */
    float vbapGainBlend = 0.f;

    /**
     * 구면 WFS(Plane wave 비활성)에서 가상 소스까지의 “유효 거리”를 방향 유지한 채 조절해
     * 파면 곡률을 바꿉니다. SPAT / Panoramix 류의 0–100–200 스케일과 유사한 의미입니다.
     * 0에 가까울수록 멀리 둔 점음원에 가깝게(평면파에 가까운 넓은 파면), 100=실제 위치(구면),
     * 200에 가까울수록 가까운 점음원처럼(나로우한 파면).
     */
    float wavefrontCurvature = 100.f;
    /**
     * Delay shape UI: 0–200, 기본 100(내부 배율 1 = 기본 WFS 지연 패턴).
     * 작을수록 스피커 간 상대 지연 차를 줄이고, 클수록 벌립니다.
     */
    float delayShapeScale = 100.f;
    /**
     * Gain shape UI: 0–200, 기본 100(지수 1 = 기본 WFS 진폭 분포).
     * 작을수록 게인 분포를 평탄하게, 클수록 피크를 강조합니다(pow 후 Σg²=1).
     */
    float gainShapeScale = 100.f;
};

} // namespace iae
