# ADR Index

Architecture Decision Records for the Spatial Engine. Confirm a record's authoritative status in its own header; this index is a navigation aid.

| # | File | Topic | Status |
|---|---|---|---|
| 0001 | `0001-process-model.md` | Process model | see file |
| 0002 | `0002-native-core-cpp-juce.md` | Native C++/JUCE core | see file |
| 0003 | `0003-ipc-osc-udp.md` | IPC over OSC/UDP | see file |
| 0004 | `0004-fdn-topology.md` | FDN reverb topology | see file |
| 0005 | `0005-algorithm-dispatch.md` | Algorithm dispatch | see file |
| 0006 | `0006-adm-osc-v1-spec-freeze.md` | ADM-OSC v1.0 spec freeze | shipped (v0.2.0) |
| 0006a | `0006-algorithm-runtime-swap.md` | Algorithm runtime swap (K=256 crossfade) | Accepted |
| 0010 | `0010-vst3-osc-binding-model.md` | VST3 OSC binding model | see file |
| 0011 | `0011-vst3-osc-multi-instance-discovery.md` | VST3 multi-instance discovery | see file |
| 0012 | `0012-adm-osc-vendor-quirks.md` | ADM-OSC vendor quirks | Reserved (placeholder) |
| 0013 | `0013-webgui-9100-producer-mode-toggle.md` | WebGUI 9100 producer-mode toggle | see file |
| 0014 | `0014-ui-tests-collection-count-disposition.md` | UI-tests collection count | see file |
| 0015 | `0015-webgui-default-mode-low-latency.md` | WebGUI default low-latency mode | see file |
| 0016 | `0016-external-distribution-policy.md` | External distribution policy | see file |
| 0017 | `0017-runtime-demote-telemetry.md` | Runtime demote telemetry | see file |
| 0018 | `0018-phase-b-sync-handlers.md` | Phase B sync handlers | Accepted (shipped `9311902`) |
| 0019 | `0019-phase-c-pcm-ipc-shm-ring.md` | Phase C PCM IPC (shm ring) | Accepted (shipping; PR1–PR5 merged, PR6/PR7 remain) |
| 0020 | `0020-sys-metrics-channel.md` | `/sys/metrics` 1 Hz telemetry channel | Accepted (shipped v0.9 Lane A) |
| — | `vid2spatial_osc_contract.md` | vid2spatial OSC contract (reference) | reference doc |

## Numbering notes
- **0006 is used by two files.** `0006-adm-osc-v1-spec-freeze.md` is the canonical "ADR 0006"; `0006-algorithm-runtime-swap.md` is labeled **`0006a`** (its H1 + cross-refs, e.g. ADR 0018 "Related"). A future cleanup may renumber `0006a → 0007`; deferred to avoid breaking inbound references.
- **0007, 0008, 0009 are unused** (gap). Reserved for the `0006a → 0007` renumber and future records.
- Status reconciliation (ADR 0018/0019 `Proposed → Accepted`, this index) was done as part of the v0.8 audit remediation P4; see `.omc/plans/spatial-engine-v0.8-audit-remediation.md`.
