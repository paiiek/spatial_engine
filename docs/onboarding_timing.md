# Onboarding Timing Log

Target: clone → build → run → modify a UI element in **≤60 minutes** on a clean Ubuntu 22.04 box
(acceptance criterion #10). If any run exceeds 60 minutes, file a `bootstrap-friction` issue
with the bottleneck step identified.

## Template (fill in after each new-machine run)

### Machine #1

| Field | Value |
|-------|-------|
| Date | TBD |
| Model | TBD |
| USB controller | TBD |
| Audio interface | TBD (Dante PCIe / USB fallback) |
| Kernel | TBD (PREEMPT_RT / commodity 6.x) |
| Time to first audio (min) | TBD |
| Notes | TBD |

### Machine #2

| Field | Value |
|-------|-------|
| Date | TBD |
| Model | TBD |
| USB controller | TBD |
| Audio interface | TBD |
| Kernel | TBD |
| Time to first audio (min) | TBD |
| Notes | TBD |

## Action threshold

- ≤60 min: pass, no action.
- 61–90 min: file `bootstrap-friction` issue P1; identify bottleneck step.
- >90 min: file `bootstrap-friction` issue P0; block next release until resolved.

## Historical runs

_(none yet — first run pending lab machine availability)_
