# WindroseTextSigns (UE4SS C++ Prototype)

## Goal
Prototype a **mod-owned** custom text flow for placed Wooden Labels using:
- F8 hotkey
- mod-side targeting
- native in-game UI path (Phase 7 in progress) with ImGui as development fallback
- sidecar JSON persistence

No native Windrose label interaction was added in this prototype.

## Current Phase Status (Unverified Until Tested)

1. Phase 1 (Discovery probe): **Implemented, pending runtime verification**
- Added ProcessEvent probe logging for construction-related function traffic.
- Added StaticConstructObject post-probe logging as a fallback when ProcessEvent is unavailable on this game/runtime.
- Logs include function path, context class/name, and GUID-like extraction attempts from params.
- Label actor detection telemetry is included when a label-like actor is seen in probe context.

2. Phase 2 (Label targeting): **Implemented as heuristic, pending runtime verification**
- F8 hotkey is implemented.
- On F8, mod resolves player view via `GetPlayerViewPoint` and selects likely label actor by camera-forward dot + distance.
- This is a camera-ray heuristic, not confirmed engine collision trace yet.
- ImGui button `Run Test (6 Signs)` runs a diagnostic that reports PASS/FAIL based on finding at least 6 unique `worldId/stableId` keys.
- Non-ImGui trigger: create `Config\run_test6.flag` under the mod folder; mod will run the same test and remove the flag file.

3. Phase 3 (Text editor UI): **Implemented, high-risk, pending runtime verification**
- ImGui tab `WindroseTextSigns` includes selected label ID, text input, Apply/Clear/Cancel.
- Added diagnostics for UI render loop and button actions:
  - `[ui] tab_render_tick`
  - `[ui] apply_clicked`
  - `[ui] clear_clicked`
- Added non-ImGui clear control:
  - `F10` clears selected/targeted label text.

4. Phase 4 (In-world rendering): **Implemented prototype, pending runtime verification**
- Runtime-managed `TextRenderComponent` create/update/remove path is now wired.
- Apply attempts to:
  - find/create a managed text render component on the selected label actor,
  - set text via `K2_SetText`/`SetText`,
  - set baseline visual settings (`SetWorldSize`, horizontal alignment, relative offset).
- Surface placement now records per-label side selection (`surfaceAxis`, `surfaceSign`) based on player camera at Apply time.
- UI has `Flip Surface Side` to quickly invert front/back placement for the selected label and persist that setting.
- UI debug tuning supports realtime surface alignment:
  - `surfaceAxis (0.00=X, 1.00=Y)` via drag control with `%.2f` precision.
  - `surfaceSign` quick toggle (`-1` / `+1`).
  - `Live surface tuning` auto-applies and persists while adjusting.
  - `Depth Off Surface` controls how far text sits out from the plaque face.
  - `Surface Align X` controls horizontal on-face alignment.
  - `Surface Align Y (Height)` controls vertical placement.
  - `Font Size` controls world text size.
  - `Text Color RGBA` controls color/alpha.
- Apply now auto-wraps and auto-sizes text using prototype constraints:
  - rows target: `1..4`
  - font size range: `10..20`
  - per-row character capacity scales down as font grows (12 at min font; max-font request of 0 is treated as 1 runtime minimum).
  - explicit newline rows entered by the player are preserved, and font is reduced to keep total vertical stack in the sign area.
- Clear attempts to destroy/unregister the managed component for that specific `worldId/stableId`.
- Diagnostic logs include:
  - `[phase4] component_created`
  - `[phase4] apply_success`
  - `[phase4] apply_failed ...`
  - `[phase4] clear_component ...`

5. Phase 5 (Persistence): **Implemented, pending runtime verification**
- Sidecar JSON is now save-profile-adjacent and role-aware.
- Dedicated server authoritative path:
  - `...\R5\Saved\SaveProfiles\Default\WindroseTextSigns\<worldId>\SignTexts.json`
- Solo/Hosted local-client authoritative path:
  - `%LOCALAPPDATA%\R5\Saved\SaveProfiles\<profileId>\WindroseTextSigns\<worldId>\SignTexts.json`
- Fallback path, used only if no save profile/world can be resolved:
  - `...\ue4ss\ModData\WindroseTextSigns\SignTexts.json`
- One-time migration attempted from legacy UE4SS paths:
  - `...\ue4ss\ModData\WindroseTextSigns\SignTexts.json`
  - `...\ue4ss\Mods\WindroseTextSigns\SignTexts.json`
- JSON metadata records `runtimeRole`, `dataMode`, `authorityMode`, `sidecarKind`, `profileRoot`, and `worldFolderId`.
- Keys are `worldId/stableId`.
- Records store text, asset, and last-seen timestamp.
- Sidecar writes are now atomic-style:
  - write to `SignTexts.json.tmp`
  - copy current file to `SignTexts.json.bak`
  - replace primary file from tmp
- Load recovery order:
  - primary `SignTexts.json`
  - backup `SignTexts.json.bak`
  - temp `SignTexts.json.tmp`
  - newest snapshot backups under the active sidecar `Backups\SignTexts.backup_*.json` folder (up to 5)
- Auto-restore hardening:
  - if primary file parses as empty while a backup/snapshot contains records, mod auto-restores from latest non-empty backup and rewrites primary.
- Malformed rows are skipped defensively instead of crashing load.
- Snapshot hardening:
  - rotating backup snapshots (max 5) are written on important save operations (`apply`, `clear`, prune batch) and throttled to avoid excessive churn.
- Destroy-prune hardening:
  - prune only runs while at least one live label is visible in the current scan.
  - missing-label threshold requires multiple consecutive misses before prune.
  - prune saves are batched in a single sidecar write per scan pass.

6. Phase 6 (Multiplayer): **Not implemented**
- The same mod package is installed on client and server.
- Dedicated server owns the authoritative sidecar.
- Solo/Hosted local worlds use the local client profile as authoritative.
- Remote clients still need the client/server bridge before their edits can become server-authoritative.

7. Phase 7 (Native in-game text entry UI): **Investigation/Scaffolding started, pending runtime verification**
- Added native capability probe for UMG/input-mode runtime entry points:
  - `/Script/UMG.UserWidget`
  - `/Script/UMG.WidgetTree`
  - `/Script/UMG.EditableTextBox`
  - `/Script/UMG.Button`
  - `AddToViewport`, `RemoveFromParent`
  - `SetInputModeGameAndUI`, `SetInputModeGameOnly`
- On F8, mod now attempts native-first editor open path and logs success/fallback reason.
- ImGui editor remains available as **development fallback** only and can be toggled from the mod tab.
- Text entry behavior continues using Phase 4 multiline autosize rules (no forced single-line prototype limit).
- Map-marker/native-text discovery probe:
  - logs only root map-marker customization popup instances when detected:
    - `[phase7-mapui] widget_open ...`
    - `[phase7-mapui] child prop=... obj=... class=...`
    - `[phase7-mapui] popup_session_open ...`
    - `[phase7-mapui] popup_text_changed ...`
    - `[phase7-mapui] popup_button_click ...` (`button=confirm|cancel`)
    - `[phase7-mapui] popup_session_close ...`
    - `[phase7-mapui] widget_closed ...`
  - logs player cursor transitions around UI open/close:
    - `[phase7-mapui] cursor_state bShowMouseCursor=true|false`
  - close-outcome heuristic is logged (`outcome=...`) using button-edge capture plus marker-count delta fallback.
  - close reason is logged (`closeReason=visibility_off|popup_not_found|popup_instance_swapped`).

8. Phase 8 (Build-menu asset discovery for no-icon Wooden Label): **Implemented diagnostic probe, pending runtime verification**
- Added BuildMenu discovery probe trigger paths:
  - Hotkey `F9`
  - file trigger `Config\run_buildmenu_probe.flag`
- Probe logs:
  - loaded `DA_BI_Utilities_Lables_Wooden_*` item objects,
  - high-signal property values for recipe/icon/class/build references,
  - candidate build-menu widget classes,
  - candidate construct/build functions for runtime injection hooks.
- Logs are emitted with prefix `[buildmenu-probe]` in `WindroseTextSigns.log`.

## Local-Only Probe Analysis

- Use the local parser to summarize Phase 8 build-menu probe output without building or deploying:

```powershell
& "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\tools\analyze_buildmenu_probe.ps1"
```

- Optional JSON output for offline review:

```powershell
& "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\tools\analyze_buildmenu_probe.ps1" `
  -OutJson "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_logs\buildmenu_probe_summary.json"
```

## Important Facts / Assumptions

- The internal spelling `Lables` is treated as canonical in asset matching.
- Stable ID extraction currently prioritizes:
1. Full building-block instance token in object name (`BuildingBlock|<GUID>|<index>`), when present.
2. GUID-like struct fields on actor/class property chains.
3. 32-hex token in full object name.
4. hashed fallback from full object name.
- Collision-based trace for label targeting is not yet proven in this build.

## Files

- Main source:
  - `C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\Source\src\SignTextMod.cpp`
  - `C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\Source\include\WindroseTextSigns\SignTextMod.hpp`
- CMake:
  - `C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\Source\CMakeLists.txt`
- Runtime config:
  - `C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\Config\WindroseTextSigns.ini`
- Manual build script:
  - `C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_windrosetextsigns.ps1`

## Build (Manual, not auto-run by agent)

Run from PowerShell:

```powershell
& "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_windrosetextsigns.ps1"
```

This script is hardened to avoid known environment pitfalls:
- normalizes process `Path`/`PATH` handling,
- uses absolute tool paths only,
- generates `.cmd` configure/build wrappers that call `VsDevCmd.bat`,
- resolves short (8.3) paths for configure/build roots to reduce cmd quoting issues,
- uses watchdog-style timeout/no-progress checks with logs in:
  - `C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_logs`

Optional:

```powershell
& "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_windrosetextsigns.ps1" `
  -SkipConfigure
```

This build script does not deploy to game client/server folders. It only builds and creates a fresh zip in:

`C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\Deployments`

## Runtime Install Layout

Expected runtime folder:

`C:\Games\WindowsServer\R5\Binaries\Win64\ue4ss\Mods\WindroseTextSigns`

Expected DLL:

`C:\Games\WindowsServer\R5\Binaries\Win64\ue4ss\Mods\WindroseTextSigns\dlls\main.dll`

6-sign test trigger file (create this file to run test once):

`C:\Games\WindowsServer\R5\Binaries\Win64\ue4ss\Mods\WindroseTextSigns\Config\run_test6.flag`

Primary sidecar path used by this mod (client example):

`C:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\ModData\WindroseTextSigns\SignTexts.json`

Legacy sidecar path that is auto-migrated if present:

`C:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\Mods\WindroseTextSigns\SignTexts.json`

## Known Limitations

- No guaranteed physics line trace yet.
- In-world rendering is now implemented as a prototype path but not yet confirmed stable across all sign variants/angles.
- Native destroy cleanup is scan-based (`prune_destroyed_label`) and requires multiple consecutive missing scans after being seen live in-session.
- Multiplayer sync is not implemented or promised.
