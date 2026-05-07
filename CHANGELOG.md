# Changelog

## 0.1.2-prototype
- Added remote-client bridge hardening for no-snapshot/UDP-failure scenarios:
  - bridge health gate with degraded/recovered state logging
  - snapshot retry backoff (5s -> 10s -> 20s -> 30s)
  - prune suppression while remote bridge is unsynced/degraded
  - explicit unsynced preview logging for optimistic remote apply/clear
- Added offline bridge fixture coverage for degraded/no-snapshot behavior and retry backoff.
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
