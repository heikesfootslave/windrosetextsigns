# Changelog

## 0.1.11
- Improved multiplayer connection reliability, especially for dedicated servers and higher-latency networks.
- Improved session/log handling so old lobby/session signals are less likely to affect a new session.
- Improved editor open responsiveness and reduced repeated setup work when opening the sign editor.

## 0.1.10
- Improved custom font autosizing and wrapping for sign text so long words and mixed-case text fit more consistently.  Font size is now based on the measured size of each character instead of assuming a fixed width.
- Fixed an edge case where a very long single word could clear the sign; overlong words are now truncated while preserving the rest of the text.
- Added a check after text renders to ensure rows don't stack on top of each other.
- Added a 'Refresh All Signs' button.  This will recalculate text layout and re-render text on all signs.
- Removed the ability for text to attach to the back of a sign.


## 0.1.9
- Fixed an issue where network route discovery could fail and retry logic would not trigger.

## 0.1.8
- Added WindroseTextSigns.ini option WTS_FORCE_LOCAL_ONLY default false.  This option forces local only sign text saves.
- Fixed a false Solo status error where `Network: Error - Sign Text file not saved` could appear on fresh worlds even after a successful first save.
- Fixed pending-world vs real-world GUID key drift that could show sign text in-world but open the F8 editor with an empty text box.
- Fixed a bug where a RemoteClient could get stuck looking at a local copy of the sign text json instead of the snapshot from the server.

## 0.1.7
- Added a Network error message indicating likely network / port configuration issues. 

## 0.1.6
- Added an in-editor `Status` section showing live `Role` and `Network` state.
- Fix an issue where the world ID could get 'lost' to the mod resulting in no communication with the sign text json.


## 0.1.5
- Reworked F8 input handling to enforce one press per action with key-release rearm, preventing duplicate opens/closes and canceling stale open requests when targeting retries are exhausted.

## 0.1.4
- Major rewrite for stability and hardening across Solo, Hosted, and Dedicated sessions.
- Added a new thematically aligned sign font option.
- Updated editor window generation/reuse for faster open time.
- Fixed first-click passthrough issues that could trigger player attack while the editor is open.
- Fixed `Esc` passthrough so it no longer incorrectly toggles the game menu while editing.
- Added editor toggle behavior: pressing the sign edit hotkey (`F8` by default) now also closes the editor.
- Improved sign sync and recovery behavior after reconnects/session transitions.
- Improved dedicated/hosted communication reliability for sidecar snapshot and delta flow.

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
