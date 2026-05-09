# Spatial Engine — Phase A Extended (M2): HOA Decoder Diversification

**Plan ID:** `spatial-engine-phaseA-extended-M2-hoa-decoders`
**Mode:** RALPLAN-DR (SHORT, with pre-mortem section)
**ETA:** ~3–4 weeks (Round-2 amendment: A-α 1–2 wk → A-γ full 4-decoder set ≈ 2× ETA)
**Owner:** core/ambi
**Created:** 2026-05-09 (Round 1) — **Amended Round 2: 2026-05-10** (Decision A: A-α → A-γ per user)
**Sprint isolation:** core/ only — no overlap with C2B postmortem sprint (vst3/ only). OFF baseline re-pin must be coordinated once after both sprints land.

---

## 0. Why This Plan

Scientist agent benchmarking flagged HOA decoder diversity as the largest market-parity gap on the M2 axis:

| Product | Decoders | Score |
|---|---|---|
| **Spatial Engine (current)** | Tikhonov pseudo-inverse only (`core/src/ambi/AmbiDecoder.cpp:1-171`) | **2 / 5** |
| Spat Revolution | basic, max-rE, in-phase, EPAD, AllRAD | 5 / 5 |
| IEM Plug-in Suite | AllRAD / max-rE / basic | 4 / 5 |
| SPARTA | AllRAD / EPAD / basic | 4 / 5 |

**Round-2 amendment (user-driven, 2026-05-10):** Adding **max-rE + AllRAD + EPAD + in-phase** (Tikhonov as fifth "basic" reference) reaches **5 / 5 algorithm-count parity at N=3**. Round-1 plan targeted 4/5 (max-rE + AllRAD only); user prioritised market parity over ETA savings, accepting ~3–4 wk sprint instead of 1–2 wk. Round-1 baseline math (`+100 % M2 score → 4/5`) is now superseded by `+150 % → 5/5`. **Round-2 Critic A9 reframe:** "5/5" is **decoder-count parity** at our current `MAX_ORDER = 3` (`core/src/ambi/AmbiDecoder.h:18`); the **order-axis parity** to N=7 (Spat Revolution / SPARTA / IEM full support) remains a separate axis deferred to a "MAX_ORDER bump" plan tracked in M2HOA-Q10.

---

## 1. Principles (5)

1. **Backward compatibility first.** Existing Tikhonov pseudo-inverse remains the default `DecoderType::PINV`. No existing call site (`AmbiDecoder::prepare(layout)` / `decode(order, sh, n, out)` in `core/src/ambi/AmbiDecoder.h:22-35`) changes signature without an overload.
2. **RT-safe contract preserved.** Audio-thread `decode()` keeps its `noexcept` guarantee with zero allocation. All matrix construction, t-design lookup, and weight tables stay in `prepare()` (control thread). Confirmed against `test_p1_rt_no_alloc` discipline (`core/tests/core_unit/`).
3. **Common SH helper reuse.** All new decoders consume `AmbisonicEncoder::encode_{1st,2nd,3rd}_order()` (`core/src/ambi/AmbisonicEncoder.cpp:6-87`) for the encoding matrix `E`. No duplicate Y_l^m formulas — single source of truth for ACN/SN3D conventions.
4. **Decoder selection is a control-thread operation.** Switching `DecoderType` triggers a `prepare()` rebuild; never mid-buffer. Audio thread reads pre-built `decode_matrices_[order-1]` exactly as today (`AmbiDecoder.cpp:148-161`).
5. **Numerical reference trail.** Each new decoder ships with a paper / SPARTA / IEM cross-check note in source comments (Daniel 2000 for max-rE weights; Zotter & Frank 2012 + IEM AllRADecoder paper for AllRAD; t-design tables from Sloane / Hardin & Sloane public-domain sets).

---

## 2. Decision Drivers (top 3)

1. **M2 market-parity score lift** — single largest scientist-flagged gap. **Round-2 target: 2/5 → 5/5** with four new decoders (full Spat Revolution parity). Round-1 was 2/5 → 4/5.
2. **Irregular speaker-layout robustness** — AllRAD specifically targets non-uniform rigs (theatre dome with under-floor gaps, asymmetric immersive setups) where pure pinv exhibits hot-spot artefacts.
3. **Algorithm portability with SPARTA / IEM / Spat** — picking the same canonical algorithms means user-side files (AllRADecoder presets, Spat decoder choice) and existing pedagogy / tutorials transfer directly to our engine. With 5/5 parity, **no decoder feature is "missing"** in market comparisons.

---

## 3. Viable Options (2 alternatives per decision point)

### Decision Point A — Decoder set (priority and breadth)

| Option | Decoders added | Pros | Cons |
|---|---|---|---|
| **A-γ (chosen, Round-2)** | **max-rE + AllRAD + EPAD + in-phase** (4 new + existing pinv = 5 total) | **Reaches 5/5** — full Spat Revolution parity in one sprint. All four algorithms are SPARTA / IEM / Spat canonical → external preset + tutorial compatibility. No M2-v2 follow-on needed for decoder count. | ETA expands 1–2 wk → ~3–4 wk (~2× cost). OFF baseline re-pin scope grows (4× new TUs × 2 hashes); higher regression risk; class TU grows ≈ 400 LOC vs Round-1 200 LOC. |
| A-α (rejected, was Round-1 chosen) | max-rE + AllRAD (2 new = 3 total) | Smallest new-code surface (~600 LOC). Fits 1–2 wk ETA. | **Leaves 2-algorithm gap** (in-phase, EPAD) → 4/5 only; M2-v2 follow-on sprint needed → cumulative effort + 2 OFF baseline re-pins instead of 1. |
| A-β (rejected) | max-rE + EPAD (2 new = 3 total) | Energy preservation guarantee (EPAD) is mathematically elegant, useful for analysis tools. | EPAD + max-rE only is **less canonical than the full set** — misses AllRAD (de-facto irregular-layout standard) and in-phase. Same M2-v2 follow-on penalty as A-α. |

**Why A-γ wins on Round-2 re-evaluation:** user explicitly prioritised **market-parity (5/5) over ETA savings**. Cumulative cost of A-α + later M2-v2 (1–2 wk + ~2 wk + extra re-pin cycle) ≈ A-γ's 3–4 wk in one sprint, with one OFF baseline re-pin instead of two. ABI churn is also amortised once.

### Decision Point B — Decoder dispatch interface

| Option | Shape | Pros | Cons |
|---|---|---|---|
| **B-α (chosen)** | `enum class DecoderType { PINV, MAX_RE, ALLRAD, EPAD, IN_PHASE }` (Round-2: 5 values) + single `AmbiDecoder` class with internal switch in `buildDecoderForOrder()` and a setter `setDecoderType(DecoderType)` invalidating cached matrices. | Zero ABI break for existing consumers (`AmbisonicRenderer` etc.). One translation unit, one symbol set per decoder family → cleaner OFF baseline diff (predictable symbol additions only, no virtual-table churn). Cache locality for `decode()` unchanged. | Class TU grows ≈ 400 LOC (Round-2 inflation from 200 LOC baseline). |
| B-β | `class AmbiDecoderBase` (pure virtual `prepare`, `decode`) + `PinvDecoder` / `MaxREDecoder` / `AllRADDecoder` / `EPADDecoder` / `InPhaseDecoder` derived classes; factory `make_decoder(DecoderType)`. | Cleaner separation; easier to fuzz one decoder at a time. | Adds vtable + heap pointer; touches every existing call site (`AmbisonicRenderer`, `RenderGraph`); larger OFF baseline diff (new vtable symbols, new heap allocations during `prepare`); more risk to RT contract because vtable lookups in `decode()` need explicit final / devirtualisation. |

**Why α (Round-2 re-confirmed):** same RT path; minimal call-site fan-out; smallest OFF baseline delta even at 5 enum values. The "extensibility" argument for B-β re-evaluation deferred until decoder count ≥ 6 (Round-2 ADR Follow-ups).

### Decision Point C — Speaker layout input

| Option | Shape | Pros | Cons |
|---|---|---|---|
| **C-α (chosen)** | Continue using `AmbiDecoder::prepare(geometry::SpeakerLayout const&)` (existing, `AmbiDecoder.h:22`). All new decoders receive the layout the same way. AllRAD additionally accepts an optional virtual-loudspeaker t-design lookup keyed by N_virt (default = 5200 points for 3rd-order). | Zero plumbing change. `LayoutLoader` (`core/src/geometry/LayoutLoader.{h,cpp}`) and YAML format untouched. | AllRAD virtual-loudspeaker count is a hidden constant — exposed via OSC schema in step S4 but not via `LayoutLoader`. |
| C-β | Extend YAML schema (`docs/ipc_schema.md`, `proto/geometry_schema.json`) with a new `decoder_hints:` block carrying `t_design_size`, `max_re_apply_per_order`. Decoders read hints from layout. | Keeps decoder tuning declarative; fully reproducible from layout file alone. | Schema bump → IPC cross-compat work, geometry_schema.json bump, parser tests, doc edits → +3 days. Not justified for two decoders shipping with sane defaults. |

**Why α:** ship sane defaults this sprint; reserve schema bump for when ≥3 tunables exist (M2-v2 will revisit).

---

## 4. Pre-mortem (3 failure scenarios)

### Scenario 1 — Round-2: 4-decoder `.rodata` + symbol inflation hits OFF dual-gate harder

**Hypothesis (Round-2 update):** With **all 4 new decoders**, total `.rodata` growth estimate is ~150–200 kB:
- AllRAD t-design tables (5200 + 100 + 24 pts × 3 × 8 B): **~125 kB** (dominant contributor, unchanged from Round-1).
- max-rE per-order weights (3 orders × 4 floats = 12 floats): **~100 B**, negligible.
- EPAD per-order energy-preserving weights + per-channel diagonal scaling factors (3 orders × ~16 doubles): **~400 B**, negligible.
- in-phase decoder weights (`g_l = (l! · (N-l)!) / ((l+N+1)! · ...)`-style table, 3 orders × 4 weights): **~100 B**, negligible.
- **New decoder TU symbols** (4 × `build_*_matrix` + helpers): adds ~20–30 named symbols to nm output → `symbols.sha256` definitely changes.

Net: `bytes.sha256` ≈ +130 kB (AllRAD-dominated), `symbols.sha256` ≈ +30 symbols. Both `.ci/off_baseline.bytes.sha256` and `.ci/off_baseline.symbols.sha256` MUST re-pin once.

**Mitigation (Round-2):**
- Place AllRAD tables in single TU `core/src/ambi/AllRADTDesigns.cpp` (one named symbol block — predictable diff for OFF reviewer).
- Place EPAD / in-phase weights as `constexpr` arrays inside their own TUs (linker can inline; minimal extra nm symbols).
- `SPATIAL_ENGINE_ALLRAD_TDESIGN_SIZE` build-time knob (default 5200, embedded fallback 1296).
- **Schedule re-pin as final step** (single CI cycle for all 4 decoders + AllRAD tables).
- **PRE-COMMIT REQUIREMENT:** capture pre-amendment baseline hashes vs post-amendment hashes in re-pin commit message — explicit diff trail for reviewer.

### Scenario 2 — max-rE weights are not sample-accurate per order

**Hypothesis:** max-rE applies per-order gain weights `g_l` (Daniel 2000, eq. 3.21) to the SH channels before pinv decode: `g_0 = 1`, `g_1 = cos(α/(N+1)) · …`. If the implementation hard-codes weights only for N=3 and silently mis-applies them at N=1 or N=2 (engine permits all three orders simultaneously per `AmbiDecoder.h:18` `MAX_ORDER = 3`), localisation will be subtly off and the existing `test_ambi_decoder_dominant_speaker` (`tests/core_unit/test_p_ambi_decoder.cpp:66`) may still pass while energy distribution is wrong.

**Mitigation:**
- New unit test `test_p_ambi_decoder_max_re.cpp` asserts per-order energy concentration (E[front] / sum > threshold[order]) **for orders 1, 2, and 3 separately** (mirrors existing `test_ambi_decoder_order_concentration` at line 187).
- Cross-check first 3 weights against SPARTA `getMaxREweights()` reference values (numerical match to 1e-6) — checked-in golden vector in test fixture.
- Document `g_l` formula and source equation reference in `MaxREDecoder.cpp` header comment.

### ~~Scenario 3 — Core-side decoder change forces TWO baseline re-pins this fortnight~~ **CLOSED (Round-2 Critic A10)**

**Round-2 Critic A10 closure:** C2B postmortem sprint already merged at commit `acb8c27` (2026-05-09); OFF baseline already re-pinned at commit `ec2510d`. **Single-cycle re-pin scope is now reduced to this sprint only** — no cross-sprint coordination needed. Risk row "OFF baseline re-pin cycle blocks CI for both sprints" downgraded from High to Medium (this sprint's solo re-pin still requires CI cycle but no longer collides with C2B). M2HOA-Q4 status flipped to closed.

**Original Round-1 hypothesis (preserved for audit trail):** This sprint changes `core/` (HOA decoders) and the parallel C2B postmortem sprint was also live (vst3/ only per `spatial-engine-phaseC-C2B-postmortem.md`); if both landed OFF-impacting commits, CI would have seen two consecutive baseline-mismatch failures.

**Round-2 actual outcome:** C2B merged + re-pinned first (ec2510d). HOA sprint will produce a single solo re-pin commit at S6.

---

## 5. Implementation Steps (Round-2 final: 9 actionable steps, 1 deferred)

**Round-2 sequence summary (with Critic A4 + A6 inserts):** **S0.5 (NEW Critic A4 — E helper refactor)** → S1 → S2 → S2.5 (EPAD) → S2.7 (in-phase) → S3 (5-enum) → S4 (`/sys/ambi_decoder_type`) → **S4.5 (NEW Critic A6 — `AmbisonicRenderer::setDecoderType` plumbing)** → S5 (4 ctest fixtures) → S6 (single coordinated re-pin) → S7 (deferred). Total active dev: ≈ **12 d** (was 10 d in Round-2 initial / 5 d in Round-1).

### S0.5 — Refactor `E` encoding-matrix construction to private static helper (≈ 0.5 day; **Round-2 Critic A4 NEW, must precede S1**)

**Why:** All 4 new decoders (`MaxRE`, `AllRAD`, `EPAD`, `InPhase`) need to construct the same `K × S` per-speaker SH encoding matrix `E` that pinv currently inlines at `core/src/ambi/AmbiDecoder.cpp:79-99`. Without this refactor, each new decoder either (a) duplicates the construction (4× silent-bug surface, especially around the ACN field-order transpose at `AmbiDecoder.cpp:88-91` where `E[1]=c.Y, E[2]=c.Z, E[3]=c.X` re-maps the legacy `AmbiCoeffs1st` field order `{W,X,Y,Z}` to ACN), or (b) calls a half-formed helper. Critic A4 escalated this to CRITICAL because the ACN re-map is exactly the kind of silent bug that passes existing tests but corrupts new decoders.

**Files modified:**
- `core/src/ambi/AmbiDecoder.h` — add `private: static void buildEncodingMatrixE(const geometry::SpeakerLayout&, int order, std::vector<double>& E);` (preallocates if capacity insufficient; documented as **control-thread only** since it allocates).
- `core/src/ambi/AmbiDecoder.cpp` — extract lines `:79-99` into the new static helper. `buildDecoderForOrder()` calls `buildEncodingMatrixE(layout, order, E)` and proceeds with the existing Tikhonov path at `:101-140` unchanged.

**Acceptance (testable):**
- **AC-S0.5.1** All 5 existing sub-tests in `core/tests/core_unit/test_p_ambi_decoder.cpp` (`dominant_speaker`, `omni`, `2nd_order_dominant`, `3rd_order_dominant`, `order_concentration` at `:257-260+`) PASS **bit-exact** post-refactor (refactor is bit-preserving — same `E` values, same Tikhonov solve, same float matrix output).
- AC-S0.5.2 New helper signature is `void` returning by reference (no `std::vector<double>` return → caller pre-allocates if hot path; aligns with RT-safe principle even though `prepare()` is control-thread).
- AC-S0.5.3 ACN field-order re-map (`AmbiCoeffs1st` `{W,X,Y,Z}` → ACN `{W,Y,Z,X}`) is preserved exactly per `AmbiDecoder.cpp:88-91` — captured in a comment block inside the new helper to prevent future engineers from "fixing" it.
- AC-S0.5.4 Subsequent decoders (S1, S2, S2.5, S2.7) all call `AmbiDecoder::buildEncodingMatrixE()` rather than re-deriving — single source of truth for the encoding matrix.

### S1 — `MaxREDecoder` weights table + builder (≈ 2 days; **Round-2 Critic A2 formula correction**)

**Algorithm (Round-2 Critic A2 fix — CORRECT FORM):** Per-order weights are the Legendre polynomial values **at the largest root of `P_{N+1}(x) = 0`**, NOT a Legendre evaluation at any "137.9°/(N+1)" angle (Round-1 fact-error: the 137.9° figure is the empirical energy-spread-angle rule of thumb for N=1, not the Legendre evaluation argument).

> Formula: `g_l = P_l(r_E,max)` where `r_E,max` is the **largest root of `P_{N+1}(x) = 0`** (Legendre polynomial of degree N+1).
>
> Numerical roots (canonical, AC-matching):
> - `r_E,max(N=1)` = 0.5774 (root of `P_2(x) = (3x²-1)/2 → x = 1/√3`); but `g_1 = P_1(0.5774) = 0.5774`. **Note:** Round-1 AC-S1.1 stated `g_1 ≈ 0.7071` which corresponds to `1/√2` — re-verify against SPARTA `getMaxREweights()` actual numerical output (see Round-2 amended AC-S1.0 below).
> - `r_E,max(N=2)` = √(3/5) ≈ 0.7746 (root of `P_3(x) = (5x³-3x)/2 → x = √(3/5)`); `g_1 = 0.7746`, `g_2 = P_2(0.7746) = 0.4000`.
> - `r_E,max(N=3)` ≈ 0.86114 (root of `P_4(x)`); `g_1 = 0.86114`, `g_2 = 0.61237`, `g_3 = 0.30474`.
>
> Reference: Zotter & Frank 2019 *Ambisonics: A Practical 3D Audio Theory* eq. 4.49; SPARTA `getMaxREweights()` C source (Politis 2018 thesis).

**Files:**
- NEW `core/src/ambi/MaxREDecoder.hpp` — declares `compute_max_re_weights(int order) -> std::array<float, 4>` returning per-order gains `g_0=1.0, g_1=r_E,max, g_2=P_2(r_E,max), g_3=P_3(r_E,max)`. Header docstring cites Zotter & Frank 2019 eq. 4.49.
- NEW `core/src/ambi/MaxREDecoder.cpp` — closed-form roots of `P_{N+1}` (precomputed constexpr) + Legendre evaluation `P_l(x)` via Bonnet's recursion (3-term, exact for `l ≤ 3`).

**Acceptance (testable):**
- **AC-S1.0 (NEW Round-2 reference re-verification)** Capture SPARTA `getMaxREweights()` output (or IEM `MaxREDecoder::getDecodingWeights()`) at `N ∈ {1, 2, 3}` and check into the test fixture as the **canonical golden vector** *before* AC-S1.{1,2,3} are finalised. If SPARTA returns `{1.0, 0.7071, …}` for N=1 as Round-1 stated, the Legendre-root derivation must be reconciled (likely SPARTA applies an additional `g_l → g_l / max(g_l)` renormalisation step, mirroring the in-phase normalisation pattern of S2.7). M2HOA-Q7 must close before this AC is locked.
- AC-S1.1 `compute_max_re_weights(1) ≈ <SPARTA-verified vector>` (tol 1e-6) — if SPARTA confirms `{1.0, 0.7071}`, the implementation must include the renormalisation step and AC-S1.0 documents the convention.
- AC-S1.2 `compute_max_re_weights(2) ≈ {1.0, 0.7746, 0.4000}` (Legendre-root canonical, tol 1e-6; SPARTA-cross-checked).
- AC-S1.3 `compute_max_re_weights(3) ≈ {1.0, 0.8611, 0.6123, 0.3045}` (Legendre-root canonical, tol 1e-6; SPARTA-cross-checked).
- AC-S1.4 No heap allocation in any function (constexpr / stack-array).
- AC-S1.5 **(NEW)** Per-order Legendre roots are computed at compile time (constexpr) — assert via `static_assert(std::abs(legendre_root_table[2] - 0.7746f) < 1e-4f)` in `MaxREDecoder.hpp`.

### S2 — `AllRADDecoder` virtual-loudspeaker pinv + spherical t-design table (≈ 4 days)

**Files:**
- NEW `core/src/ambi/AllRADDecoder.hpp` — declares `build_allrad_matrix(int order, geometry::SpeakerLayout const&, int n_virtual = 5200) -> std::vector<float>` returning `S × K` decode matrix.
- NEW `core/src/ambi/AllRADDecoder.cpp` — algorithm: (1) load t-design points, (2) pinv-decode SH → virtual loudspeakers, (3) for each virtual loudspeaker run VBAP onto real layout (reuse `core/src/render/Vbap.*` if present, else inline 3D VBAP), (4) compose final matrix `D_AllRAD = G_VBAP · D_pinv_virt`.
- NEW `core/src/ambi/AllRADTDesigns.cpp` — read-only spherical t-design coordinate tables for N_virt ∈ {24, 100, 5200} (Hardin & Sloane public-domain source; license note at file head).

**Acceptance (testable):**
- AC-S2.1 For uniform 24-speaker tetrahedral-derived layout, `D_AllRAD` matches `D_pinv` within 1 % Frobenius norm (uniform layouts should agree).
- AC-S2.2 For irregular 9-speaker hemi-dome (front-heavy), AllRAD produces no negative-gain entries > -0.05 (energy-preserving property).
- AC-S2.3 Build-time table size ≤ 200 kB total `.rodata` (`size --format=sysv` on `libspe_core.so`).
- AC-S2.4 No heap allocation in `decode()` path; allocations only in `prepare()`.

### S2.5 — `EPADDecoder` energy-preserving builder (≈ 1.5 days, NEW Round-2; **Round-2 Critic A4 + A7 corrections**)

**Algorithm (Round-2 Critic A7 fact-correction):** EPAD (Energy-Preserving Ambisonic Decoding, Zotter & Frank 2012, ICSA paper). Modifies pinv decode by enforcing per-direction energy preservation: `D_EPAD = U · diag(σ̂) · V^T` where `[U, σ, V] = svd(E)` and `σ̂_k = sqrt(σ_k² / sum(σ²))` rescaled to unit total energy. **Calls `AmbiDecoder::buildEncodingMatrixE()` per S0.5** (Critic A4 — single source of truth for `E`; replaces Round-1's "reuses pinv's encoding matrix `E` from `:79-99`" wording, which implied direct line-range reuse).

**SVD implementation (Round-2 Critic A7 — IMPLEMENT FROM SCRATCH, no `solveSPD` reuse):**
- Implements **two-sided Jacobi rotation** on the symmetric `S × S` matrix `E E^T` (when `S ≤ K`, the smaller well-conditioned form) or `K × K` matrix `E^T E` (when `K < S`) — pick whichever is smaller for numerical conditioning (M2HOA-Q11 update).
- ~80 LOC of new code in `EPADDecoder.cpp`.
- **Round-1 wording "extends existing `solveSPD()` pattern at `:34-62`" was wrong** — `solveSPD()` is a Gauss-Jordan **inverse** routine, NOT an eigendecomposition; the two algorithms share zero code. The Tikhonov-regularisation pattern at `AmbiDecoder.cpp:114-120` (λ = 1e-6 · trace/S diagonal load) IS reused as the EPAD fallback when convergence fails.

**Files:**
- NEW `core/src/ambi/EPADDecoder.hpp` — declares `build_epad_matrix(int order, geometry::SpeakerLayout const&) -> std::vector<float>` returning `S × K` decode matrix.
- NEW `core/src/ambi/EPADDecoder.cpp` (~150 LOC) — Jacobi-rotation SVD from scratch + EPAD weight composition. Single TU; constexpr per-order normalisation constants.

**Acceptance (testable):**
- AC-S2.5.1 For uniform 24-speaker layout, `D_EPAD` total energy `||D_EPAD · y||²` per source direction matches within 5 % across 100 sampled directions (energy-preservation property).
- AC-S2.5.2 For irregular 9-speaker hemi-dome, EPAD energy variance across sampled directions ≤ pinv variance (EPAD should equalise hot-spots).
- AC-S2.5.3 Numerical reference: per-order singular value spectrum matches SPARTA `getEPADdecoder()` output within 1e-5 Frobenius norm — golden vector checked into test fixture.
- AC-S2.5.4 No heap allocation in `decode()`; SVD computation lives in `prepare()` only.
- **AC-S2.5.5 (NEW Round-2 Critic A7)** Jacobi sweep cap = **100 iterations**; off-diagonal convergence tolerance = `1e-12 · trace(A)`. On non-convergence, log a `LogLevel::WARN` line via `core/src/util/Trace.h` and **fall back to PINV** (Tikhonov via the `AmbiDecoder.cpp:114-120` pattern). Test fixture in `test_p_ambi_decoder_epad.cpp` includes a deliberately degenerate 4-speaker collinear layout that forces convergence failure → asserts the fallback path triggers AND returns a numerically valid pinv matrix.
- **AC-S2.5.6 (NEW Round-2 Critic A7)** If post-Jacobi condition number `σ_max² / σ_min² > 1e10`, fall back to Tikhonov-regularised PINV (do **not** silently return ill-conditioned EPAD — that would produce audio-thread NaN). EPAD `prepare()` latency budget ≤ **30 ms** for a 16-speaker layout (S3 latency budget table updated accordingly: was < 20 ms in Round-2 initial).

### S2.7 — `InPhaseDecoder` weights builder (≈ 1 day, NEW Round-2; **Round-2 Critic A1 normalisation fix**)

**Algorithm (Round-2 Critic A1 fix — option (b) two-step):** In-phase decoding (Daniel 2000, eq. 3.30 — also Malham 1992). Two-step formulation chosen over the equivalent closed form for code-audit clarity:

1. **Raw kernel** `g_l_raw = (N!)² / ((N-l)! · (N+l+1)!)` — yields `g_0_raw = (N!)² / (N! · (N+1)!) = 1/(N+1)`, NOT 1.0. This is the unnormalised Daniel 2000 kernel.
2. **Normalisation** so `g_0_normalised = 1.0` (matches AC-S2.7.{1,2,3} golden vectors and SPARTA convention): divide all `g_l_raw` by `g_0_raw = 1/(N+1)`, equivalently multiply by `(N+1)`.

**Equivalent closed form (option (a) of Critic A1):** `g_l_normalised = (N+1) · (N!)² / ((N-l)! · (N+l+1)!) = (N+1)! · N! / ((N-l)! · (N+l+1)!)`. The two-step implementation is mathematically identical but documents the convention auditably and matches SPARTA `getInPhaseweights()` line-by-line.

**Citation chain:** Daniel 2000 §3.30 (raw kernel) + SPARTA `getInPhaseweights()` C source (normalisation convention; Politis 2018 thesis). Heller et al. 2014 AES "Is My Decoder Ambisonic?" uses the same `g_0 = 1` convention — non-conflicting cross-reference.

**Files:**
- NEW `core/src/ambi/InPhaseDecoder.hpp` — declares `compute_in_phase_weights(int order) -> std::array<float, 4>` returning per-order **normalised** gains `g_0 = 1.0, g_1 … g_order`. Header docstring documents the two-step derivation.
- NEW `core/src/ambi/InPhaseDecoder.cpp` (~120 LOC) — closed-form factorial weights (constexpr). Composition with pinv mirrors S1 max-rE pattern (composition direction depends on M2HOA-Q2 — must resolve before S1; see Appendix B).

**Acceptance (testable):**
- **AC-S2.7.0 (NEW Round-2 sanity)** `compute_in_phase_weights_raw(N)[0]` returns `1/(N+1)` exactly for `N ∈ {1, 2, 3}` (raw-kernel sanity check before normalisation; tol 1e-7). Catches the silent bug where the engineer forgets the normalisation step.
- AC-S2.7.1 `compute_in_phase_weights(1) ≈ {1.0, 0.3333}` (Daniel 2000 + SPARTA reference, tol 1e-6).
- AC-S2.7.2 `compute_in_phase_weights(2) ≈ {1.0, 0.5000, 0.1000}` (tol 1e-6).
- AC-S2.7.3 `compute_in_phase_weights(3) ≈ {1.0, 0.6000, 0.2000, 0.02857}` (tol 1e-6).
- AC-S2.7.4 No heap allocation; constexpr / stack-array only.
- AC-S2.7.5 **N>3 limitation:** Engine `MAX_ORDER = 3` (`core/src/ambi/AmbiDecoder.h:18`); Spat Revolution supports N=7. Documented as known parity gap (see Open Question M2HOA-Q10) — does NOT block 5/5 decoder-count parity goal.

### S3 — `AmbiDecoder` dispatch + ABI-preserving extension (≈ 1 day; Round-2: 5-value enum)

**Files modified:**
- `core/src/ambi/AmbiDecoder.h` — add `enum class DecoderType { PINV=0, MAX_RE=1, ALLRAD=2, EPAD=3, IN_PHASE=4 }` (default `PINV`, explicit values for ABI stability); add `void setDecoderType(DecoderType)` (control thread); add private member `DecoderType type_ = DecoderType::PINV;`.
- `core/src/ambi/AmbiDecoder.cpp` — `buildDecoderForOrder()` switches on `type_`: `PINV` keeps current path (`AmbiDecoder.cpp:74-140` unchanged), `MAX_RE` calls `MaxREDecoder::compute_max_re_weights()` then composes with pinv, `ALLRAD` calls `AllRADDecoder::build_allrad_matrix()`, **`EPAD` calls `EPADDecoder::build_epad_matrix()`**, **`IN_PHASE` calls `InPhaseDecoder::compute_in_phase_weights()` then composes with pinv (mirrors `MAX_RE` composition pattern)**. `decode()` body untouched (`AmbiDecoder.cpp:142-162`).

**Acceptance (testable):**
- AC-S3.1 Existing tests `test_p_ambi_decoder` (5 sub-tests in `tests/core_unit/test_p_ambi_decoder.cpp:257-260+`) all PASS unchanged when `DecoderType::PINV` (default) — proves backward-compat (bit-exact regression).
- AC-S3.2 `setDecoderType(<any>)` followed by `prepare(layout)` rebuilds matrices in < 5 ms for a 16-speaker layout, < 50 ms for `ALLRAD` with N_virt=5200 (control-thread budget; AllRAD is the slowest of the 5).
- AC-S3.3 `decode()` audio-thread function symbol signature unchanged in `nm libspe_core.so` output (modulo new internal helpers under `MaxRE_*`/`AllRAD_*`/`EPAD_*`/`InPhase_*` namespace prefixes).
- AC-S3.4 5-value enum dispatch covered by switch with `default:` fallback to `PINV` (clamps malformed values, matches `decode()` clamp behaviour at `AmbiDecoder.cpp:145`).

### S4 — OSC / Constants surface (≈ 0.5 day; **Round-2 Critic A3 namespace fix**)

**Round-2 Critic A3 fact-correction:** Round-1 plan stated `/spe/ambi/decoder_type` and referenced `OscDispatcher`, both of which are wrong. Verified codebase facts:
- All system verbs use the `/sys/...` namespace (`core/src/ipc/CommandDecoder.cpp:316,321,326,329,337` — `/sys/handshake`, `/sys/algo_swap`, `/sys/reset`, `/sys/ambi_order`, `/sys/ltc_chase`).
- **Zero `/spe/...` verbs exist** in `proto/ipc_schema.md` or `core/src/ipc/CommandDecoder.cpp`.
- The class is `CommandDecoder` (in `core/src/ipc/`), not `OscDispatcher` — no `OscDispatcher` symbol exists in the codebase.
- The structural template for the new verb is the existing `/sys/ambi_order` handler at `core/src/ipc/CommandDecoder.cpp:329-337` (sibling verb; same payload pattern).

**Files modified:**
- `core/src/core/Constants.h` — add `constexpr int kAmbiDecoderType_PINV=0`, `kAmbiDecoderType_MaxRE=1`, `kAmbiDecoderType_AllRAD=2`, `kAmbiDecoderType_EPAD=3`, `kAmbiDecoderType_InPhase=4`.
- `core/src/ipc/CommandDecoder.cpp` — add new branch in if/else chain at `:329-337` pattern, after `/sys/ambi_order`: `} else if (addr == "/sys/ambi_decoder_type") { … }`. New `CommandTag::SysAmbiDecoderType` enumerator + `PayloadSysAmbiDecoderType { uint8_t type; }` mirroring `PayloadSysAmbiOrder` shape.
- `core/src/ipc/CommandDecoder.cpp` (forward path, ~line 644 region) — emit `addr = "/sys/ambi_decoder_type"` for the reverse encode (mirrors existing `/sys/ambi_order` encode at `:644`).
- `docs/ipc_schema.md` + `proto/ipc_schema.md` — document new OSC address `/sys/ambi_decoder_type i {0|1|2|3|4}` (Round-2: corrected namespace + 5 values).
- `proto/geometry_schema.json` — **NOT** changed this sprint (Decision C-α).

**Acceptance (testable):**
- AC-S4.1 (Round-2 reword) New OSC handler added to `core/src/ipc/CommandDecoder::decode()` if/else chain following the `/sys/ambi_order` pattern at `CommandDecoder.cpp:329-337`; no schema-version bump (additive only). Cross-reference `/sys/ambi_order` decoder branch as structural template; reverse-encode path mirrors `:644`.
- AC-S4.2 `test_p_adm_osc.cpp` discipline followed: malformed type values (e.g. `5`, `-1`) clamp to PINV (matches `/sys/ambi_order` clamp pattern at `CommandDecoder.cpp:333-335`), do not crash.
- AC-S4.3 `docs/ipc_schema.md` diff reviewed by user before commit.
- AC-S4.4 All 5 decoder type integers have explicit OSC docs entries with algorithm name + paper reference (Daniel 2000 / Zotter & Frank 2012 / Hardin & Sloane attribution).
- AC-S4.5 **(NEW Round-2 backfill)** While editing `proto/ipc_schema.md`, **backfill the existing `/sys/ambi_order` documentation** — currently undocumented in `proto/ipc_schema.md` despite being implemented at `CommandDecoder.cpp:329`. Co-locating the two `/sys/ambi_*` verbs in the schema doc avoids inconsistency and reduces reviewer confusion.

### S4.5 — `AmbisonicRenderer::setDecoderType` plumbing (≈ 0.5 day; **Round-2 Critic A6 NEW**)

**Why:** S3 adds `AmbiDecoder::setDecoderType(DecoderType)` but offers no path from the OSC/IPC layer to actually invoke it on the live decoder instance held by the renderer. Without S4.5, the new OSC verb at S4 has no terminal effect — Critic A6 escalated this to MAJOR because the plan was structurally incomplete.

**Verified codebase facts (Round-2):**
- The `AmbiDecoder` instance lives at `core/src/render/AmbisonicRenderer.h:37` (`ambi::AmbiDecoder decoder_;`).
- The renderer is owned by `SpatialEngine` at `core/src/core/SpatialEngine.h:94` (`render::AmbisonicRenderer ambisonic_;`).
- The existing `setOrder` plumbing pattern is at `AmbisonicRenderer.h:30-34` (atomic, RT-safe API: "Safe to call from any thread; the renderer picks up the change at the start of the next audio block.") and the bridge from IPC payloads to the renderer is at `core/src/core/SpatialEngine.cpp:320` (`ambisonic_.setOrder(static_cast<int>(qc.ambi_order));`). [Round-2 R2 Note 1 fix: `:287` corrected to `:320`.]
- The new plumbing mirrors this exact pattern, NOT a free-standing handler.

**Files modified:**
- `core/src/render/AmbisonicRenderer.h` — add public method (mirroring `setOrder` shape at `:32`):
  - `void setDecoderType(int type) noexcept;` — atomic, RT-safe API; valid range `0..4` (clamped to `PINV` on out-of-range).
  - Add private `std::atomic<int> decoder_type_{0};` member next to `order_` at `:38`.
- `core/src/render/AmbisonicRenderer.cpp` — at the top of `processBlock` (next-block boundary, mirroring how `order()` is loaded at `:69` `if (active_order == 1) …`):
  - Load `decoder_type_.load(std::memory_order_relaxed)`. If it differs from the last applied value (cached as a non-atomic field), call `decoder_.setDecoderType(...)` then `decoder_.prepare(layout_)` on the **control-thread side** of the prepareToPlay/processBlock boundary. Note: `decoder_.prepare()` allocates and is **not** RT-safe; the safest pattern is to perform the rebuild lazily on the next `prepareToPlay()` cycle OR to expose a separate `applyPendingDecoderTypeChange()` control-thread call invoked by `SpatialEngine` outside `processBlock`. **Recommended pattern: control-thread rebuild only**, with `processBlock` doing nothing on a pending change beyond reading the latest atomic — `SpatialEngine` calls a control-thread `applyPendingDecoderTypeChange()` between blocks (mirrors the algo-swap crossfade pattern at `core/tests/core_unit/test_p3_algoswap_crossfade.cpp`).
- `core/src/core/SpatialEngine.cpp` — add a new dispatch arm after the existing `setOrder` call at `:287`:
  - `case CommandTag::SysAmbiDecoderType: ambisonic_.setDecoderType(static_cast<int>(qc.ambi_decoder_type)); break;` — and call `ambisonic_.applyPendingDecoderTypeChange()` on the control-thread tick.

**Acceptance (testable):**
- **AC-S4.5.1** `AmbisonicRenderer::setDecoderType(int)` is callable from any thread; the actual `decoder_.prepare()` rebuild happens on the **control-thread boundary** (verified by inserting a sleep/yield in `processBlock` and confirming no allocation occurs there per the `test_p1_rt_no_alloc` discipline). The setter itself is `noexcept` and only writes one atomic.
- AC-S4.5.2 End-to-end OSC verb test: send `/sys/ambi_decoder_type i 2` via the `test_p4_command_decode.cpp` discipline; assert `SpatialEngine` invokes `ambisonic_.setDecoderType(2)` and on the next control-thread tick `decoder_.type_ == ALLRAD`.
- AC-S4.5.3 Out-of-range OSC value (`i 7`) clamps to `PINV` (`type=0`) at the IPC boundary (`CommandDecoder.cpp` clamp matches `:333-335` pattern from `/sys/ambi_order`); renderer never sees `decoder_type_ > 4`.
- AC-S4.5.4 RT-safety: `processBlock` performs zero allocation when `decoder_type_` changes — the rebuild is deferred to control-thread; renderer documents the deferral pattern in the `AmbisonicRenderer.h` setter docstring.

### S5 — New ctest fixtures (≈ 3 days, Round-2: 4 fixtures)

**Files:**
- NEW `core/tests/core_unit/test_p_ambi_decoder_max_re.cpp` — 4 sub-tests:
  - `dominant_speaker_max_re_order1/2/3` (mirror existing pinv tests at `tests/core_unit/test_p_ambi_decoder.cpp:66-181`)
  - `weights_match_sparta_reference` (golden numerical vector AC-S1.{1,2,3})
- NEW `core/tests/core_unit/test_p_ambi_decoder_allrad.cpp` — 3 sub-tests:
  - `uniform_layout_matches_pinv` (AC-S2.1)
  - `irregular_layout_no_negative_hotspots` (AC-S2.2)
  - `t_design_table_loaded` (sanity: first/last point coordinates match published Hardin & Sloane)
- **NEW `core/tests/core_unit/test_p_ambi_decoder_epad.cpp`** — 3 sub-tests (Round-2):
  - `uniform_layout_energy_preserved` (AC-S2.5.1, ≤ 5 % energy variance across 100 directions)
  - `irregular_layout_energy_better_than_pinv` (AC-S2.5.2)
  - `singular_values_match_sparta` (AC-S2.5.3 golden vector)
- **NEW `core/tests/core_unit/test_p_ambi_decoder_in_phase.cpp`** — 3 sub-tests (Round-2):
  - `weights_match_daniel2000_reference` (AC-S2.7.{1,2,3} golden vectors)
  - `dominant_speaker_in_phase_order1/2/3` (mirror pinv pattern)
  - `no_rear_lobe_negative_gains` (validates in-phase smoothing property: all decode-matrix entries ≥ -0.01 for any speaker direction)
- `core/tests/core_unit/CMakeLists.txt` — register all 4 new targets via existing `add_test()` macro pattern (mirrors `test_p_ambi_decoder` registration).

**Acceptance (testable):**
- AC-S5.1 All 4 new test binaries compile and `ctest -L ambi` runs `p_ambi_decoder` + `p_ambi_decoder_max_re` + `p_ambi_decoder_allrad` + `p_ambi_decoder_epad` + `p_ambi_decoder_in_phase` (5 binaries total).
- AC-S5.2 All assertions PASS in CI with no warnings from `-Wall -Wextra`.
- AC-S5.3 RT no-alloc discipline test (existing `test_p1_rt_no_alloc.cpp`) extended to exercise all 5 decoder types' `decode()` paths.
- AC-S5.4 SPARTA / IEM / Daniel 2000 numerical references for all 4 new decoders are checked-in golden vectors with paper-equation citations in test fixture comments.

### S6 — OFF baseline re-pin (solo, ≈ 0.5 day, **last**; Round-2 Critic A10 simplified)

**Files modified:**
- `.ci/off_baseline.bytes.sha256` — new sha for `libspe_core.so` (or `.a`) reflecting added TUs (4 new decoders + AllRAD t-design rodata).
- `.ci/off_baseline.symbols.sha256` — new sha for `nm --defined-only` symbol list (4 decoder symbol blocks added).

**Acceptance (testable):**
- AC-S6.1 `vst3.yml` GHA workflow OFF dual-gate PASS on first run after re-pin.
- AC-S6.2 Re-pin commit message lists all 4 new decoder TUs + AllRAD rodata size delta (`size --format=sysv` before/after) for reviewer audit; references this plan ID and the closed Pre-mortem Scenario 3 / C2B precedent commit `ec2510d`.
- ~~AC-S6.3~~ **Removed (Round-2 Critic A10):** C2B coordination clause obsolete; this sprint re-pins solo on top of the post-C2B baseline.

### S7 — DEFERRED: WebGUI / VST3 param exposure

**Why deferred:** vst3/ surface is owned by C2B postmortem sprint this fortnight. Exposing `decoder_type` as a VST3 param requires touching `vst3/Source/...` which conflicts. Track in `.omc/plans/open-questions.md` for next sprint.

---

## 6. Risks & Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| OFF baseline re-pin cycle blocks CI for this sprint (Round-2 Critic A10 downgraded from "for both sprints") | Medium (CI red 0–1 day) | C2B sprint already merged + re-pinned at `ec2510d` (2026-05-09); this sprint's S6 produces a single solo re-pin commit. No cross-sprint coordination required. Pre-mortem Scenario 3 closed. |
| AllRAD t-design table license ambiguity | Medium (legal) | Use Hardin & Sloane public-domain coordinates; license note in `AllRADTDesigns.cpp` header. Cross-check IEM AllRADecoder source uses same set (Open Question M2HOA-Q6). |
| **Per-order weights wrong (max-rE / in-phase / EPAD)** — silent bugs at N=1,2 (Round-2 expanded) | Medium (subtle quality regression × 3 decoders) | Per-order golden vectors vs SPARTA / Daniel 2000 in AC-S1.{1,2,3} + AC-S2.5.3 + AC-S2.7.{1,2,3}; per-order localisation / energy tests in AC-S5. |
| **EPAD SVD numerical instability on degenerate layouts** (Round-2 NEW) | Medium | Reuse Tikhonov regularisation pattern from `AmbiDecoder.cpp:114-120` (λ = 1e-6 · trace/S); fall back to PINV gracefully if `σ_min < 1e-12`; logged warning, not crash. |
| `prepare()` rebuild latency exceeds control budget on layout change | Low–Medium (UX hiccup) | AC-S3.2 enforces < 5 ms for 16-spk pinv/max-rE/in-phase, < 50 ms for AllRAD (N_virt=5200), < 20 ms for EPAD SVD. |
| **5-way switch dispatch in `buildDecoderForOrder()` becomes structurally fragile at ≥ 6 decoders** (Round-2 Critic A8 reword — ABI itself is stable as long as enumerator values are explicit) | Low | Explicit enumerator values 0..4 (S3); B-β refactor trigger at ≥ 6 decoders documented in ADR Follow-ups. **Round-2 contradiction-fix:** dropped the "reserve values 5–7" pre-allocation language (M2HOA-Q3 Round-2 update aligned with this risk row — enum grows naturally as needed, not pre-reserved). |
| Binary-size budget violation on embedded targets | Low | `SPATIAL_ENGINE_ALLRAD_TDESIGN_SIZE` build knob (Pre-mortem 1); EPAD/in-phase weights are `constexpr` (negligible footprint). |
| **In-phase decoder N>3 parity gap vs Spat Revolution N=7** (Round-2 NEW, known limitation) | Low (functional parity 5/5 still met) | Engine `MAX_ORDER=3` (`AmbiDecoder.h:18`) constrains all 5 decoders equally; documented in Open Question M2HOA-Q10 + Spec note. Spat Revolution N=7 support deferred to a separate "MAX_ORDER bump" plan. |

---

## 7. Success Criteria (sprint-level)

- [ ] All 4 new decoder TUs compile clean under `-DSPATIAL_ENGINE_NO_JUCE=ON`: `MaxREDecoder.{hpp,cpp}`, `AllRADDecoder.{hpp,cpp}`, `AllRADTDesigns.cpp`, **`EPADDecoder.{hpp,cpp}`** (Round-2), **`InPhaseDecoder.{hpp,cpp}`** (Round-2).
- [ ] `ctest --output-on-failure` passes including 5 existing pinv assertions + **13 new ambi assertions across 4 fixtures** (Round-2: was 7 across 2 fixtures): `p_ambi_decoder_max_re` (4) + `p_ambi_decoder_allrad` (3) + `p_ambi_decoder_epad` (3) + `p_ambi_decoder_in_phase` (3).
- [ ] `python3 -m pytest` regression suite passes.
- [ ] `DecoderType::PINV` (default) is bit-exact identical to current Tikhonov path (regression baseline preserved per AC-S3.1).
- [ ] OFF dual-gate (`bytes.sha256` + `symbols.sha256`) GREEN after coordinated re-pin commit.
- [ ] **Scientist M2 score re-evaluation: 2/5 → 5/5** (Round-2: full Spat Revolution parity, was 4/5).
- [ ] No regression in `test_p_ambi_decoder.cpp` 5 sub-tests (`dominant_speaker`, `omni`, `2nd_order_dominant`, `3rd_order_dominant`, `order_concentration`) at `tests/core_unit/test_p_ambi_decoder.cpp:257-260+`.
- [ ] Documentation: `docs/ipc_schema.md` + `proto/ipc_schema.md` updated for `/sys/ambi_decoder_type` OSC verb with all 5 enum values + paper attribution (AC-S4.4). [R2 Note 4 fix: `/spe/ambi/decoder_type` corrected to `/sys/ambi_decoder_type`.]
- [ ] **Numerical verification trail (Round-2):** Each of 4 new decoders has checked-in golden numerical reference (SPARTA / IEM / Daniel 2000 / Zotter & Frank 2012) inside its test fixture (AC-S5.4).
- [ ] **AllRAD t-design table license check (Round-2):** Hardin & Sloane public-domain attribution verified vs IEM AllRADecoder source (Open Question M2HOA-Q6 closed).

---

## 8. ADR (Architecture Decision Record)

**Decision (Round-2, amended 2026-05-10):** Add **`MAX_RE` + `ALLRAD` + `EPAD` + `IN_PHASE`** decoder modes (4 new) to existing `spe::ambi::AmbiDecoder` via an internal 5-value `enum class DecoderType` switch (Option **A-γ** + B-α + C-α). Default behaviour remains Tikhonov pseudo-inverse (`PINV`). **Round-1 decision was A-α (max-rE + AllRAD only); user re-prioritised market parity over ETA.**

**Decision Drivers:**
1. **M2 market-parity score lift (2/5 → 5/5 algorithm-count at N=3)** — full Spat Revolution **decoder-count** parity in one sprint instead of two; eliminates the algorithm-count gap entirely. Order-axis parity (N=3 → N=7) is a separate axis deferred to a future "MAX_ORDER bump" plan (M2HOA-Q10).
2. Irregular speaker-layout robustness (AllRAD specifically targets non-uniform rigs).
3. Algorithm portability with SPARTA / IEM / Spat (canonical algorithms → user-side preset + tutorial compatibility).

**Alternatives considered:**
- **A-α (max-rE + AllRAD only — Round-1 chosen):** Rejected on Round-2 — saves 1–2 wk ETA but leaves 2-algorithm gap (in-phase + EPAD); requires M2-v2 follow-on sprint; cumulative ETA ≈ A-γ but with 2× OFF baseline re-pin cycles.
- **A-β (max-rE + EPAD only):** Rejected — leaves AllRAD irregular-layout gap unfilled; less canonical coverage than A-γ; same M2-v2 follow-on penalty as A-α.
- **B-β (`AmbiDecoderBase` virtual hierarchy with 5 derived classes):** Rejected — vtable in `decode()` complicates RT contract; new heap allocation in `prepare()`; larger OFF symbol churn; "extensibility" gain still not decisive at 5 decoders. Re-evaluation trigger raised to ≥ 6 decoders.
- **C-β (YAML schema bump for `decoder_hints:` block):** Rejected — IPC schema bump cost > value while AllRAD `n_virtual` is the only meaningful runtime tunable across all 5 decoders.

**Why Chosen (A-γ + B-α + C-α — Round-2):**
- **5/5 Spat Revolution parity in one sprint** — user explicit priority over ETA savings.
- Cumulative effort vs A-α + M2-v2 follow-on is ≈ neutral (~3–4 wk in one go vs ~1–2 wk + ~2 wk + extra re-pin), but **OFF baseline re-pin happens once instead of twice** and ABI churn (5-value enum) is amortised once.
- Zero ABI break for existing `AmbisonicRenderer` and downstream consumers (B-α preserved; only enum widens additively with explicit values 0..4).
- Predictable OFF baseline diff (additive symbols only; one AllRAD rodata block; EPAD/in-phase weights are constexpr/negligible).
- Preserves bit-exact backward-compat default (`PINV` per AC-S3.1).

**Consequences:**
- ✅ **Spat Revolution decoder-count parity at 5/5 (N=3 axis)** (vs Round-1's 3/5); SPARTA / IEM algorithm-count parity at 3/3 canonical decoders. **Round-2 Critic A9 caveat:** order-axis parity (N=3 → N=7) is NOT closed by this sprint — see Follow-ups + M2HOA-Q10.
- ✅ Marketing claim **"AllRAD + EPAD + max-rE + in-phase compatible at orders 1-3"** unlocks irregular-layout + research/analysis customer segments simultaneously. The phrase deliberately bounds the claim to "at orders 1-3" to avoid implying N=7 support we don't have.
- ⚠️ ETA expands 1–2 wk → ~3–4 wk (~2× cost, user-accepted trade-off).
- ⚠️ `AmbiDecoder` class TU grows ≈ 400 LOC (Round-2: was 200 LOC in Round-1).
- ⚠️ Binary `.rodata` grows ~150–200 kB total (AllRAD t-design dominant; EPAD/in-phase weights negligible); guarded by `SPATIAL_ENGINE_ALLRAD_TDESIGN_SIZE` build knob.
- ⚠️ One-time coordinated OFF baseline re-pin required; sequencing constraint with C2B sprint (Pre-mortem 3).
- ⚠️ EPAD adds SVD numerical-stability surface (Risk row); mitigated by Tikhonov regularisation fallback.

**Follow-ups (post-Phase A Extended, 2026-Q3 onward):**
- B-β virtual hierarchy refactor: trigger raised to **decoder count ≥ 6** (Round-1 trigger was ≥ 4, now obsolete). Round-2 Critic A8: trigger is structural (5-way switch fragility), not ABI; ABI remains stable as long as enumerator values are explicit.
- Add `decoder_hints:` block to `proto/geometry_schema.json` (Option C-β) once ≥ 3 declarative tunables exist.
- VST3 / WebGUI exposure of `decoder_type` parameter (S7 deferred — owned by C2B-follow-on / GUI sprint).
- **MAX_ORDER bump (N=3 → N=7)**: separate plan to match Spat Revolution / SPARTA full 7th-order support; impacts encoder + all 5 decoders; binary size + RT cost surge → independent ADR (Open Question M2HOA-Q10).

---

## 9. Coordination Notes (Round-2 Critic A10 simplified)

- **Parallel sprint (Round-1 concern, now closed):** C2B postmortem (`spatial-engine-phaseC-C2B-postmortem.md`) merged at `acb8c27` (2026-05-09) and OFF baseline re-pinned at `ec2510d`. Git conflict risk with this sprint: zero (vst3/ vs core/ files).
- **OFF baseline (Round-2):** **solo re-pin commit** for this sprint at S6 end. No combined commit needed. Single CI cycle.
- M2HOA-Q4 (Round-1 cross-sprint coordination tracker) status flipped to closed in `open-questions.md`.

---

## 10. File-Level Change Manifest

| File | Status | Lines (est., Round-2) |
|---|---|---|
| `core/src/ambi/AmbiDecoder.h` | MODIFY | +24 (5-value enum + setter + private static `buildEncodingMatrixE` per S0.5; Round-2 Critic A8: dropped "reserved values" comment) |
| `core/src/ambi/AmbiDecoder.cpp` | MODIFY | +75 (S0.5 helper extraction from `:79-99` + 5-way dispatch in `buildDecoderForOrder`) |
| **`core/src/render/AmbisonicRenderer.h`** (Round-2 Critic A6) | MODIFY | +8 (`setDecoderType` API + `decoder_type_` atomic + `applyPendingDecoderTypeChange()`) |
| **`core/src/render/AmbisonicRenderer.cpp`** (Round-2 Critic A6) | MODIFY | +30 (control-thread rebuild path + `processBlock` atomic load) |
| **`core/src/core/SpatialEngine.cpp`** (Round-2 Critic A6) | MODIFY | +8 (dispatch arm at `:320` region for `CommandTag::SysAmbiDecoderType`) [R2 Note 1 fix: `:287` → `:320`] |
| **`core/src/ipc/CommandDecoder.cpp`** (Round-2 Critic A3) | MODIFY | +20 (new `/sys/ambi_decoder_type` decode branch at `:329-337` pattern + reverse encode at `:644` region) |
| `core/src/ambi/MaxREDecoder.hpp` | NEW | ~30 |
| `core/src/ambi/MaxREDecoder.cpp` | NEW | ~80 |
| `core/src/ambi/AllRADDecoder.hpp` | NEW | ~40 |
| `core/src/ambi/AllRADDecoder.cpp` | NEW | ~250 |
| `core/src/ambi/AllRADTDesigns.cpp` | NEW | ~100 (mostly data) + ~5200 numeric literals |
| **`core/src/ambi/EPADDecoder.hpp`** (Round-2) | NEW | ~35 |
| **`core/src/ambi/EPADDecoder.cpp`** (Round-2) | NEW | ~150 (SVD via Jacobi) |
| **`core/src/ambi/InPhaseDecoder.hpp`** (Round-2) | NEW | ~30 |
| **`core/src/ambi/InPhaseDecoder.cpp`** (Round-2) | NEW | ~120 (factorial weights + composition) |
| `core/src/core/Constants.h` | MODIFY | +9 (Round-2: 5 enum constants vs Round-1's 3) |
| `core/CMakeLists.txt` | MODIFY | +8 (Round-2: 4 new TUs vs Round-1's 2) |
| `core/tests/core_unit/test_p_ambi_decoder_max_re.cpp` | NEW | ~180 |
| `core/tests/core_unit/test_p_ambi_decoder_allrad.cpp` | NEW | ~200 |
| **`core/tests/core_unit/test_p_ambi_decoder_epad.cpp`** (Round-2) | NEW | ~180 |
| **`core/tests/core_unit/test_p_ambi_decoder_in_phase.cpp`** (Round-2) | NEW | ~180 |
| `core/tests/core_unit/CMakeLists.txt` | MODIFY | +12 (Round-2: 4 new targets vs Round-1's 2) |
| `docs/ipc_schema.md` | MODIFY | +20 (Round-2: 5 enum value docs vs Round-1's 3) |
| `proto/ipc_schema.md` | MODIFY | +20 |
| `.ci/off_baseline.bytes.sha256` | RE-PIN | 1 line (single combined re-pin, Pre-mortem 3) |
| `.ci/off_baseline.symbols.sha256` | RE-PIN | 1 line |

**Total new/modified C++ LOC (Round-2 final):** ≈ **1180** (vs Round-1's ~600; Round-2-initial ~1100; Round-2-final added +80 LOC for S0.5 + S4.5 + Critic A6 plumbing).
**Total test LOC (Round-2 final):** ≈ 740 (unchanged from Round-2-initial; new ACs reuse fixture infrastructure).
**Net change vs Round-1:** +580 source LOC, +360 test LOC, +2 new decoder TUs (EPAD + in-phase), +2 new ctest fixtures, +2 enum values, **+1 solo OFF baseline re-pin cycle** (Round-2 Critic A10 closed cross-sprint coordination — no longer "single combined", just "single solo").

---

## 11. Round-2-Initial Amendment Changelog (superseded by Appendix C — kept for audit trail)

User-driven amendment: Decision A from **A-α (max-rE + AllRAD, 4/5 score)** → **A-γ (max-rE + AllRAD + EPAD + in-phase, 5/5 score)**. Trade-off: ETA 1–2 wk → ~3–4 wk accepted in exchange for full Spat Revolution decoder parity.

| # | Section | Round-1 → Round-2 delta |
|---|---|---|
| A1 | Header `ETA`, §0 Why, §2 Drivers #1, §3 Decision A table, §3 Decision B enum, §4 Pre-mortem Scenario 1 | ETA 1–2 wk → ~3–4 wk; M2 target 4/5 → 5/5; Decision A chosen flips α→γ; B-α enum widens to 5 values (`PINV`/`MAX_RE`/`ALLRAD`/`EPAD`/`IN_PHASE`); Pre-mortem 1 rodata estimate refined (4 decoders, AllRAD-dominant). |
| A2 | §5 Implementation Steps | NEW S2.5 (`EPADDecoder` SVD-based, ~150 LOC, 1.5 d) + NEW S2.7 (`InPhaseDecoder` factorial weights, ~120 LOC, 1 d). S3 enum dispatch widened to 5 values. S5 ctest fixtures: 2 → 4 (`epad`, `in_phase` added). Total active dev: 5 d → ~10 d. |
| A3 | §8 ADR | Decision: `A-α + B-α + C-α` → `A-γ + B-α + C-α`. Drivers: 2/5 → 4/5 → **2/5 → 5/5**. Alternatives: A-α now in rejected list; A-γ moved to chosen with rationale (cumulative ETA neutral, single OFF re-pin, ABI churn amortised). Consequences: rodata +130 kB → +150–200 kB; class TU +200 LOC → +400 LOC; new SVD numerical-stability surface (EPAD). Follow-ups: B-β refactor trigger raised ≥4 → ≥6 decoders; M2-v2 EPAD/in-phase row removed (delivered Round-2). |
| A4 | §7 Success Criteria + §6 Risks | 7 → 13 new ctest assertions across 4 fixtures. Added: numerical-verification trail row (paper attribution per decoder), AllRAD t-design license check row. Risk row "max-rE weights wrong" expanded to "Per-order weights wrong (max-rE / in-phase / EPAD)"; new "EPAD SVD numerical instability" row; new "in-phase N>3 parity gap" known-limitation row. |
| A5 | `.omc/plans/open-questions.md` | M2HOA-Q9 (EPAD weights paper citation precision), M2HOA-Q10 (in-phase / decoder N>3 parity vs Spat Rev N=7) appended. M2HOA-Q5 (S7 deferral) and M2HOA-Q3 (enum reservation) updated for 5-value enum reality. |

**Cross-sprint coordination unchanged:** parallel C2B postmortem sprint (vst3/ only) still has zero git overlap. Single combined OFF baseline re-pin still required at fortnight end (Pre-mortem 3 mitigation unchanged). HOA sprint ETA expansion (1–2 wk → 3–4 wk) means C2B postmortem will likely land first; HOA decoder PR will rebase + final re-pin commit captures both sprints' deltas.

**Round-2 file:line citations preserved 80%+:** all new acceptance criteria reference `core/src/ambi/AmbiDecoder.cpp:74-140` (pinv path), `:142-162` (RT decode), `:34-62` (Gauss-Jordan reused for EPAD SVD), `:114-120` (Tikhonov regularisation reused for EPAD fallback); `AmbiDecoder.h:18` (`MAX_ORDER=3` in-phase parity gap reference); `tests/core_unit/test_p_ambi_decoder.cpp:66-181, :257-260+` (existing assertions baseline); `AmbisonicEncoder.cpp:6-87` (SH helper reuse Principle 3); `.ci/off_baseline.{bytes,symbols}.sha256` (re-pin targets); `geometry/LayoutLoader.{h,cpp}` (untouched per C-α).

---

## Appendix B — Critic "What's Missing" Items (Round-2 backfill)

Surface-area items raised by Critic R1 that are not single-line ACs but plan-wide invariants. Captured here so reviewers can audit them as a block.

### B1 — M2HOA-Q2 (composition direction) MUST resolve before S1 starts

`max-rE` and `in-phase` decoders both compose per-order weights `g_l` with the pinv decoding matrix. Two equivalent formulations exist and produce **different intermediate matrix shapes**:

- **Pre-decode (SH-side multiply):** apply `diag(g_l)` to the K-channel SH input *before* the pinv `S × K` decode. Engineering pattern: store one S × K decode matrix per `DecoderType`, weights baked in at `prepare()` time. Memory: 4 × S × K floats.
- **Post-decode (speaker-side multiply):** decode with raw pinv `D_pinv` then multiply each speaker output by a direction-dependent gain. Engineering pattern: store one S × K matrix shared across max-rE / in-phase + a per-speaker gain table. Memory: 1 × S × K + 2 × max-spk floats.

Pre-decode is the canonical SPARTA / IEM choice and is what AC-S1.{1,2,3} + AC-S2.7.{1,2,3} numerical reference implies. **Decision must be locked before S1 implementation starts** — wrong direction silently produces correct golden-vector values but wrong audio. Resolution lives in `open-questions.md` M2HOA-Q2 (Round-2 update).

### B2 — `t_design_table_loaded` static_assert sanity guard (Round-2 NEW)

S2 AllRAD t-design tables (`AllRADTDesigns.cpp`) are coordinate arrays sized at `n_virtual ∈ {24, 100, 5200}`. Silent bug surface: someone swaps the literal table data without updating the `n_virtual` constant, producing a sub-table that compiles but loads zero-padded points. Mitigation: **compile-time assertion** in `AllRADTDesigns.cpp`:

```cpp
static_assert(sizeof(kTDesign5200) / sizeof(TDesignPoint) == 5200,
              "AllRAD t-design 5200-point table size mismatch");
```

Test fixture also asserts at runtime that first/last point coordinates match the published Hardin & Sloane reference (already in S5 `t_design_table_loaded` sub-test).

### B3 — RT-safe guard comment at decode entry (Round-2 NEW)

`AmbiDecoder::decode()` at `core/src/ambi/AmbiDecoder.cpp:142` is the single audio-thread entry point. To prevent a future engineer from "helpfully" adding a `switch (type_) { … }` inside `decode()` (which would re-introduce branch-misprediction overhead AND tempt them to compute weights inline), add a load-bearing comment block immediately above the function:

```cpp
// CONTROL THREAD ONLY responsibility: do NOT add any switch on type_ here.
// All decoder-type-specific logic lives in prepare() / buildDecoderForOrder().
// See Principle 2 of the HOA Decoder Diversification plan.
```

This is purely a documentation / review-discipline change. Captured here so the executor adds the comment in S3 alongside the dispatch logic.

### B4 — `proto/ipc_schema.md` backfill discipline

Per AC-S4.5 (Round-2): the existing `/sys/ambi_order` verb is implemented at `CommandDecoder.cpp:329` but undocumented in `proto/ipc_schema.md`. While editing the schema doc to add `/sys/ambi_decoder_type`, the executor MUST backfill the `/sys/ambi_order` row to keep the two `/sys/ambi_*` verbs co-located. This is treated as an in-sprint side-quest, not deferred work.

---

## Appendix C — Round-2 Changelog (Critic R1 fixes)

| ID | Source | Severity | Section impact | Round-1 (post-amend) | Round-2 final |
|---|---|---|---|---|---|
| A1 | Critic #1 | CRITICAL | §S2.7 algorithm + AC-S2.7.0..5 | Raw kernel `g_l = (N!)² / ((N-l)! · (N+l+1)!)` (would yield `g_0 = 1/(N+1) ≠ 1.0`, contradicting golden vectors) | Two-step: raw kernel + explicit normalisation by `(N+1)`; AC-S2.7.0 added (raw `g_0 = 1/(N+1)` sanity); SPARTA + Heller 2014 cross-citation |
| A2 | Critic #2 (Architect missed) | CRITICAL | §S1 algorithm + AC-S1.0..5 | `g_l = legendre_P_l(cos(137.9°/(N+1)))` (numerically wrong; 137.9° is energy-spread rule of thumb, not Legendre arg) | `g_l = P_l(r_E,max)` where `r_E,max` = largest root of `P_{N+1}(x)=0`; AC-S1.0 added (re-verify SPARTA reference before locking AC-S1.1); Zotter & Frank 2019 eq. 4.49 citation; constexpr `static_assert` added |
| A3 | Critic #3 (Architect missed) | CRITICAL | §S4 OSC verb + AC-S4.1..5 | `/spe/ambi/decoder_type i` + `OscDispatcher` (both fact-errors — codebase has zero `/spe/...` verbs and no `OscDispatcher` class) | `/sys/ambi_decoder_type i {0..4}` (sibling of `/sys/ambi_order` at `CommandDecoder.cpp:329-337`); AC-S4.1 reworded to `CommandDecoder::decode()`; AC-S4.5 added (backfill `/sys/ambi_order` schema doc) |
| A4 | Critic #4 (severity LOW→CRITICAL escalation) | CRITICAL | NEW §S0.5 + AC-S0.5.1..4 | (none — Round-2-initial assumed inline `E` reuse) | NEW step S0.5: refactor `E` construction at `AmbiDecoder.cpp:79-99` into private static `buildEncodingMatrixE()`; ACN field-order re-map at `:88-91` preserved as a comment block (silent-bug guard); ETA +0.5d |
| A5 | Critic #5 / Architect #1 | MAJOR | §3 Decision B + §8 ADR Consequences + §9 Coordination | `AmbiRendererNode` (does not exist) | `AmbisonicRenderer` (verified at `core/src/render/AmbisonicRenderer.h:18,37`; owner at `SpatialEngine.h:94`); 3 sites updated via `replace_all` |
| A6 | Critic #6 / Architect #4 | MAJOR | NEW §S4.5 + AC-S4.5.1..4 | (none — `setDecoderType` was added but no plumbing path to renderer) | NEW step S4.5: `AmbisonicRenderer::setDecoderType(int)` API mirroring `setOrder` at `:32`; control-thread rebuild via `applyPendingDecoderTypeChange()`; `SpatialEngine.cpp:287` dispatch arm; ETA +0.5d |
| A7 | Critic #7 / Architect #3 | MAJOR | §S2.5 algorithm + AC-S2.5.5..6 | "extends `solveSPD()` pattern at `:34-62`" (factual nonsense — Gauss-Jordan inverse, not eigendecomposition) | "Implements two-sided Jacobi rotation from scratch (~80 LOC); `solveSPD` shares zero code with eigendecomposition"; AC-S2.5.5 (sweep cap 100, tolerance 1e-12, fall-back to PINV via `:114-120` Tikhonov); AC-S2.5.6 (condition number > 1e10 → fallback); EPAD latency budget 30 ms (was 20 ms) |
| A8 | Critic #8 | MAJOR | §6 Risk row "ABI fragile" + M2HOA-Q3 | M2HOA-Q3 said "Reserve values 5-7"; §6 risk row said "ABI fragile at ≥6" — mutually contradictory | §6 reworded to "5-way switch dispatch becomes structurally fragile at ≥6 — reason for B-β trigger, NOT ABI breakage. ABI stable with explicit enumerator values"; M2HOA-Q3 update aligned (drop "reserve 5-7") |
| A9 | Critic #9 / Architect #7 | MINOR | §0 + §2 driver #1 + §8 ADR Consequences | "5/5 Spat Revolution full parity" (overclaim — order axis N=3 vs Spat's N=7 unaddressed) | "5/5 algorithm-count parity at N=3 (order axis to N=7 deferred to MAX_ORDER bump plan, M2HOA-Q10)"; marketing claim bounded with "at orders 1-3" |
| A10 | Critic #10 / Architect #6 | MINOR | §4 Scenario 3 + §6 risk row + §9 + M2HOA-Q4 | Scenario 3 "two re-pin cycles" + "single combined re-pin" coordination — obsolete after C2B merged at `acb8c27` | Scenario 3 marked CLOSED (note pinned to `acb8c27` + `ec2510d`); §6 OFF row downgraded High→Medium; §9 simplified to "solo re-pin"; M2HOA-Q4 closed in `open-questions.md` |

**ETA delta (Round-2 final):**
- Round-1: 5d active dev → 1–2 wk calendar.
- Round-2-initial (A-γ flip): 10d active dev → 3–4 wk calendar.
- **Round-2-final (Critic R1 fixes): 12d active dev (+0.5d S0.5 + +0.5d S4.5 + +1d S1/S2.5/S2.7 numerical re-verification + S4 namespace plumbing inflation)** → 3–4 wk calendar (banner unchanged; buffer absorbs).

**Recommendation:** Round-3 verification — Architect skip, Critic-only single round to verify the 10 fixes landed correctly. If clean, plan exits to autopilot.
