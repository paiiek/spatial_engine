# License — spatial_engine v0

`spatial_engine` is licensed under the **GNU General Public License v3.0 (GPL-3.0-or-later)**
for v0 (lab / research / internal use).

## Why GPL v3

The native audio core depends on **JUCE 7.x**, which is dual-licensed (GPL or commercial).
For v0 development we use the GPL track, which means **all source distributed with v0 is GPL-3
and any binaries derived from it inherit the GPL obligations** when distributed.

## Trigger event for commercial license

A JUCE commercial license (Indie / Pro / Perpetual) **must be procured** before either of:

1. The first **external distribution** of any binary built from this repository.
2. The first **commercial deployment** outside the user's research lab (performance, exhibition,
   client install, productized release).

See `docs/license_procurement_plan.md` for owner, options, costs, and timeline.

## Contributor reminder

PRs must be **GPL-compatible** (no proprietary blobs, no copyleft-incompatible licenses,
no contributor-license-agreement that re-licenses contributors' code under more permissive
terms than GPL v3 — the project may re-license at the trigger event with a clean room
rewrite if needed, but contributions inherit GPL v3 by default).

The full GPL v3 text is reproduced (when the JUCE submodule is initialized) at
`core/JUCE/LICENSE.md`. The canonical text also lives at <https://www.gnu.org/licenses/gpl-3.0.txt>.

— v0 sign-off; revisit at trigger event.
