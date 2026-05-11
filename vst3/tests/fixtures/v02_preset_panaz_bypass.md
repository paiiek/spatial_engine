# v02_preset_panaz_bypass.vstpreset

Binary state fixture captured from v0.2.0 plugin writer (C4-S2.5).

## Format
v2 binary, 36 bytes, little-endian:
- bytes 0-3:  magic 0x31455053 ('SPE1' LE)
- bytes 4-5:  version uint16 = 2
- bytes 6-7:  param_count uint16 = 7
- bytes 8-35: 7 x float32 normalized values

## Parameter values encoded
| Index | ParamId      | Value |
|-------|-------------|-------|
| 0     | kPanAz       | 0.7   |
| 1     | kPanEl       | 0.3   |
| 2     | kSourceWidth | 0.5   |
| 3     | kMasterGain  | 0.6   |
| 4     | kAmbiOrder   | 0.2   |
| 5     | kRoomPreset  | 0.8   |
| 6     | kBypass      | 1.0   |

## Expected reader output (test_vst3_state_v3_reader_only Row 2)
- setState() returns kResultOk
- kBypass = 1.0 (loaded from stream)
- kMute = 0.0 (default — not present in v2 stream)
- All 6 base params match values above

## Soak window
Committed at S2.5 (2026-05-11). Soak until S8 (5 days minimum per D3-γ contract).
The v3 writer landing at S7 must produce byte-compatible reads on these v2 fixtures.
