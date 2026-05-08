# WindroseTextSigns Agent Notes

Derived from session: `019ddfe0-2e60-77f1-9c58-63ee854fbf0e`

## Project goal
- Keep native Wooden Labels intact.
- Add editable player text to placed labels using runtime logic (UE4SS + C++).
- Prefer production behavior over prototype/dev workflow noise.

## Confirmed game/content facts
- Game is UE5 (`Windrose 0.10.0.4.268` observed during session).
- Label assets are spelled `Lables` in content paths.
- Build/craft content is under `Plugins/R5BusinessRules/Content/...`.
- Wooden Labels appear under `Storage And Beds > Wooden Labels`.

## Product behavior to preserve
- Player builds a normal native wooden label.
- Player looks at it and presses hotkey (default `F8`) to edit text.
- Apply/Clear/Cancel flow updates world text and persistence.
- Dedicated server flow must keep working.
- Solo flow must save and restore text across relog.

## Runtime architecture expectations
- Runtime features are UE4SS/DLL-driven; this is not a `.pak`-only mod today.
- Bridge roles must be explicit:
- `DedicatedServer`: authoritative bridge role.
- `RemoteClient`: receives/requests updates.
- `Solo local-authoritative`: keep local JSON/rendering; do not enter UPnP/bridge host path.

## Critical Solo invariant
- On startup restore, first scan may restore records but must not prune as destroyed.
- Prune only after live labels have been seen in a stable scan window.
- This prevents relog data loss where signs save correctly but disappear next load.

## Content/package stance
- Content package deployment is opt-in (default is no content pak deployment).
- Keep stale content package cleanup enabled so old test paks do not mask runtime behavior.

## QA expectations
- Keep offline QA checks for:
- Solo prune timing invariant.
- Bridge role routing correctness.
- Deploy script surface staying opt-in for content paks.
- Keep logs concise by default; verbose logging should be opt-in.

## Repo hygiene (production-facing)
- Do not publish dev-only folders in main public footprint:
- `_scratch`, `assets`, `build_scripts`, `design`, `relay/cloudflare-worker`, `tools`, `LuaMods` (per latest repo policy from session).
- Keep README user-facing (install/use/config/limits), not planning diary.

## Troubleshooting order
1. Confirm DLL loads and mod startup logs are present.
2. Confirm hotkey edge is logged when pressing `F8`.
3. Confirm route/role is expected (`Solo` vs `Dedicated` vs `Remote`).
4. Confirm sidecar JSON write/read succeeds.
5. If text applies but vanishes on relog, inspect prune timing/state gates first.

## Rules
- Do not build/complile code unless explicitly asked to.  Keep "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_windrosetextsigns.ps1" up to date for manual code build/compile.
- Do not deploy to game folders unless explicitly asked to.  Keep "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\deploy_TextSigns_clean.ps1" up to date for manual packaging and deployment.
- Keep C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\tools\run_offline_qa.ps1" up to date with latest findings and always run it against changes made.
- Before analyzing any new runtime logs which are not already in \WindroseTextSigns\log_archives, create a local archive snapshot first using `tools\archive_log_analysis.ps1` (include provided paths + known defaults) and record the archive path in notes/results.
