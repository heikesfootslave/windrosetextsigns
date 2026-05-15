# Changelog

## Unreleased - Curved text Stage 1 (per-sign + live editor)
- Per-sign curve amount: each sign now persists its own `curveAmount` value in `SignTexts.json` instead of a single global `WTS_CURVE_ARC_AMOUNT` for all signs.
- Range expanded from `[0.0, +1.0]` (only convex/bow-up) to `[-1.0, +1.0]` (concave/sag, flat, convex/bow).
- New hotkey workflow for adjusting curvature live in-game:
  - `F6` (configurable via `WTS_CURVE_HOTKEY_TOGGLE`) toggles Curve Edit Mode on the currently targeted sign.
  - `PageUp` / `PageDown` (configurable via `WTS_CURVE_STEP_UP` / `WTS_CURVE_STEP_DOWN`) step the active sign's curve in `WTS_CURVE_STEP` increments (default 0.1).
  - Edit Mode auto-exits when looking at a different sign or away from any sign.
  - Each step writes through to the sidecar immediately, so curvature survives restarts.
- Fit-to-plaque font sizing: the per-glyph render now scales the font to fill the plaque instead of starting from the stored `font_size` and only shrinking. Should resolve the "text microscopic" regression observed with longer strings (e.g. "Zum fliegenden Hollander").
  - `LETTER_HALF_WIDTH_RATIO` 0.275 -> 0.20
  - `STRIDE_RATIO` 0.55 -> 0.40
  - `MIN_FONT_SIZE` 8 -> 14, new `MAX_FONT_SIZE` 48 ceiling
- Config:
  - `WTS_CURVE_DEFAULT_FOR_NEW_SIGNS` replaces `WTS_CURVE_ARC_AMOUNT` (legacy key still honored for backwards compatibility).
- Save format:
  - JSON sidecar regex extended with an optional 17th capture group for `curveAmount`; signs saved before this version load with `curve_amount = 0` (flat).
  - Equality comparator now treats `curve_amount` changes as a dirty-write trigger.

## 0.1.3
- Added F8 input latency breakdown telemetry (per-press edge->target->construct->open timing) and default-enabled ini control.
- Improved Phase7 teardown policy to reduce false in-session UI teardown/auto-close during transient readiness churn.
- Added bootstrap/session reset hardening with expanded epoch state clears and additional reset diagnostics.
- Hardened RemoteClient route bootstrap with stricter loopback handling and direct-connect fallback candidate extraction from connection logs.
- Improved restore/apply resiliency with bounded short retries for transient component-create failures.
- Updated log retention policy:
  - increased cap to 2MB
  - append across sessions
  - bounded bootstrap-history retention budget

## 0.1.2-prototype
- Hardened role/bootstrap flow around session-ready gating to reduce role/route churn during world-load transitions.
- Improved authoritative prune safety and stale-record handling across Solo/Hosted/Dedicated paths, including additional suppression/confirm telemetry.
- Expanded replay/offline diagnostics with dedicated-server session pairing and dedicated prune-path assertions.
- Added visual verification debug instrumentation to help diagnose “apply succeeded but text not visible” scenarios.
- Added remote-client bridge hardening for no-snapshot/UDP-failure scenarios:
  - bridge health gate with degraded/recovered state logging
  - snapshot retry backoff (5s -> 10s -> 20s -> 30s)
  - prune suppression while remote bridge is unsynced/degraded
  - route candidate set + rotation for same-machine bind ambiguity (`127.0.0.1` vs NIC IP) and multi-NIC/VPN LAN ambiguity
  - deferred `prune_rebuilt_label` while remote client has not received first authoritative snapshot
  - dedicated authoritative fast-prune when a label key rebinds to a different actor instance (fixes stale text resurrecting on destroy/rebuild with quick rebuild timing)
  - remote-client rebuild render guard to suppress stale snapshot/upsert text flashes while awaiting authoritative prune/clear for rebuilt keys
  - solo authoritative prune gate now requires "seen live this session" before a record can be pruned as destroyed
  - dedicated authoritative prune gate now defers pruning never-seen records until authority source is resolved, active-world warmup time has elapsed, and probable-label scans are stable
  - added explicit prune deferred telemetry for live-guard/world-count blocking reasons
  - explicit unsynced preview logging for optimistic remote apply/clear
- Added offline bridge fixture coverage for degraded/no-snapshot behavior and retry backoff.
- Added adaptive UPnP mode selection for bridge hosts:
  - new `WTS_BRIDGE_UPNP_MODE` (`off|on|auto`) with backward-compatible fallback from `WTS_BRIDGE_UPNP_ENABLED`
  - non-blocking asynchronous UPnP mapping attempts (startup/tick no longer block on router COM calls)
  - `auto` policy based on observed bridge client endpoint classes (`loopback/private/public`) so same-machine and LAN-only sessions avoid unnecessary mapping
  - policy can escalate later in the same host session when public clients appear
- Added log noise reduction and retention controls:
  - compacted `[phase4] apply_success` logging to keep only the key-identifying prefix
  - suppression of consecutive duplicate log payloads with repeat summary lines
  - single-session log lifecycle (fresh `WindroseTextSigns.log` each play session)
  - bootstrap session markers: `[session] bootstrap_begin` and `[session] bootstrap_end`
  - 1MB rolling cap that preserves bootstrap head and latest tail content

## 0.1.0-prototype
- Added `WindroseTextSigns` UE4SS C++ mod skeleton.
- Added Phase 1 diagnostics for placement/build-related ProcessEvent probing.
- Added Phase 2 mod-owned F8 targeting flow with camera-forward selection heuristic.
- Added Phase 3 ImGui editor tab (Apply/Clear/Cancel).
- Added Phase 5 sidecar JSON persistence (`SignTexts.json`).
- Added staged Phase 4 placeholder logging for in-world render component attach path.
