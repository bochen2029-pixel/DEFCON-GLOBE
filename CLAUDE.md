# DEFCON 3D-Globe Conversion

## What this project is

A refactor of Introversion Software's **DEFCON v1.6** (2006, C++, Visual Studio 2010, DirectX 9 / OpenGL) from a 2D equirectangular-map RTS into a **true 3D-globe** RTS. The source is the official game release. Gameplay feel, rules, units, economy, and AI strategy stay recognizable; the *world* it is played on changes from a flat Mercator surface to a sphere, with altitude as a real dimension.

The lobby already shows a spinning 3D globe — this project takes that visual and makes it the actual gameplay surface.

## Intent

- Preserve DEFCON's identity: ten-nation nuclear brinkmanship, escalating DEFCON levels, slow-then-sudden pacing, vector-era aesthetic.
- Replace the 2D equirectangular play surface with a sphere. Missiles arc over the poles. Bombers fly under the horizon. Radar respects curvature.
- Do **not** rewrite. Refactor. Lean on what's already there — `Vector3<Fixed>` velocities, the `LobbyRenderer` globe, the `EarthData` island/border/city polylines, the `Fixed` deterministic math layer.
- Keep the game deterministic and network-synchronous at its 10 Hz tick. No desync regressions.

## Non-goals

- Not a full graphics overhaul. No modern PBR, no 3D unit models — silhouettes and icons still read at a glance.
- Not a rules rework. Unit counts, DEFCON gating, alliance rules, victory conditions unchanged.
- Not multi-platform re-targeting. Stay on the existing VS2010 / DirectX 9 target first; macOS/Linux parity follows after the Windows build is green.
- No engine swap. Keep the in-house SDL + DirectX stack, the Eclipse GUI, the Tosser containers, the NetLib protocol.

## Core architectural invariants (do not violate)

1. **Deterministic fixed-point everywhere in the simulation.** All world-state math uses `Fixed`. Any new spherical trig (great-circle distance, sphere→Cartesian, ray-sphere intersection used by gameplay) must be `Fixed`-exact and bit-identical across platforms. Floats are allowed in the renderer only.
2. **10 Hz fixed server tick.** Client-side prediction and interpolation are the only place framerate-dependent code lives.
3. **Lon/lat stays the authoritative serialized position.** Altitude is added as a third field. Cartesian XYZ is a *derived* representation computed per-tick for physics and rendering — never the source of truth. This preserves save-file and protocol shape and limits churn.
4. **Network protocol changes are a versioned break.** When altitude lands in the wire format, bump the protocol version; do not try to run mixed old/new clients.
5. **Code style of the original.** Hungarian `m_` / `g_` prefixes, no namespaces, raw pointers, pre-C++11. Don't modernize incidentally — it pollutes diffs and makes the conversion illegible.

## What fundamentally changes

| Subsystem | Change |
|---|---|
| Position | `(lon, lat)` → `(lon, lat, altitude)`. Altitude is a `Fixed` in meters (or km — pick one and document at [source/world/worldobject.h](source/world/worldobject.h)). |
| Distance | 2D Euclidean → great-circle (surface) or 3D chord (through-globe). New helper in `lib/math/`. |
| Movement | Linear lon/lat interpolation → great-circle paths. `CrossSeam` and `m_targetLongitudeAcrossSeam` deleted — a sphere has no seam. |
| Nuke trajectory | Fake 2D arc (`m_curveDirection`) → sub-orbital ballistic arc with apogee. Nukes have a real altitude profile. |
| Radar | `RadarGrid` flat 360×200 lat/lon grid → spherical coverage. Radar horizon becomes a function of altitude: ground radar can't see over the curve, airborne/space-based can. |
| Camera | 2D pan+zoom → orbit camera (yaw, pitch, distance). Selection = ray-vs-sphere pick, not inverse-Mercator. |
| Renderer | `MapRenderer` (flat) → `GlobeRenderer` (sphere). Promote the techniques already in [source/renderer/lobby_renderer.h](source/renderer/lobby_renderer.h) to gameplay. |
| Whiteboard | Screen-space strokes → surface-space strokes on the sphere (texture-baked or geodesic polyline). |
| Fleet pathing | 2D navigability bitmap (`bmpSailableWater`) still usable via lon/lat lookup; path search becomes spherical A*. |
| Save / replay / mod format | Breaks. Clean-break version bump; no migration path attempted in Phase 1. |

## What does not change

Game state machine, team/AI structure, economy, unit types, DEFCON escalation, alliances, voting, chat, achievements, auth/key system, in-house libraries (Eclipse GUI, Tosser, NetLib), build system (VS2010 + DirectX SDK).

## Phased plan (risk-ordered)

Each phase should be shippable on its own — the build runs, the game plays, determinism holds.

- **Phase 0 — Rendering-only globe.** `GlobeRenderer` alongside `MapRenderer`, toggleable. Game logic still 2D; world positions projected onto a sphere for display only. Proves the pipeline without touching simulation.
- **Phase 1 — Spherical distance & paths.** Replace every 2D distance comparison with great-circle. Movement follows geodesics. Delete `CrossSeam`. No altitude yet. Radar still flat-grid-backed but distance-corrected.
- **Phase 2 — Altitude.** Add the Z axis to position. Bombers/fighters/nukes have real flight envelopes. Radar horizon activates. Protocol version bump.
- **Phase 3 — Camera, input, UI.** Orbit controls, ray-pick selection, world→screen projection for labels, back-of-globe culling.
- **Phase 4 — Polish.** Day/night terminator, atmosphere, trail rendering in 3D, whiteboard re-design.
- **Phase 5 — AI re-tuning, mod/save migration, platform parity.**

## Risk hot-spots

- **`Fixed` trig determinism.** If `sin`/`cos`/`atan2` on `Fixed` aren't already bit-identical across compilers, every client will desync within minutes of a match. Audit [lib/math/](source/lib/) early. If gaps exist, implement them before Phase 1.
- **Trajectory accumulation error.** Integrating a nuke over hundreds of 10 Hz ticks in `Fixed` without drift takes care. Prefer closed-form Kepler at each tick over incremental integration.
- **Radar data structure.** A naive sphere-covering grid has pole distortion. Options: HEALPix, geodesic (icosphere) grid, or keep flat lat/lon grid and correct cell weights. Cheapest wins unless profiling says otherwise.
- **UI regressions.** Tooltips, action menus, popup dialogs all assume screen-space positions from a 2D world. Every anchor needs a `project(lon, lat, alt) -> screen` pass. Label occlusion by the globe itself is a new class of bug.
- **Mod authors.** A 2D map mod is meaningless on a sphere. Communicate the break; don't try to silently reinterpret old mods.

## Entry points for a cold session

- Game loop: [source/defcon.cpp](source/defcon.cpp)
- App singleton / wiring: [source/app/app.h](source/app/app.h)
- Game state machine: [source/app/game.h](source/app/game.h)
- World / master state: [source/world/world.h](source/world/world.h)
- Unit base class: [source/world/worldobject.h](source/world/worldobject.h)
- Mobile unit base: [source/world/movingobject.h](source/world/movingobject.h)
- Nuke trajectory (the single biggest physics change): [source/world/nuke.h](source/world/nuke.h)
- Radar grid (the biggest data-structure change): [source/world/radargrid.h](source/world/radargrid.h)
- 2D renderer (the thing being replaced): [source/renderer/map_renderer.h](source/renderer/map_renderer.h)
- 3D renderer seed crystal (the template to follow): [source/renderer/lobby_renderer.h](source/renderer/lobby_renderer.h)
- Earth data (coastlines, borders, cities — reused as-is): [source/world/earthdata.h](source/world/earthdata.h)
- Network protocol (where altitude will be added): [source/network/network_defines.h](source/network/network_defines.h)

## Guardrails for edits

- When in doubt, preserve the original function signature and add a new one rather than change the old. Easier to diff, easier to bisect.
- New code belongs under phase-specific folders when non-trivial (`source/renderer/globe/…`, `source/world/sphere/…`). Don't scatter.
- Every behavioural change should be reproducible in a headless determinism test (two simulated clients, same seed, N ticks, state hashes match). Set this up in Phase 0 and never let it go red.
- Build target stays `Release | Win32` in `targets/msvc/DefconX.sln` until parity is reached. No incidental platform cleanup.
