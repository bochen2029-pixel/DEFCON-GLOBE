# DEFCON 3D-Globe Conversion — Design v1

Design baseline for converting Introversion Software's DEFCON v1.6 (2006, C++, VS2010, DirectX 9 / OpenGL) from a 2D equirectangular-map RTS to a true 3D-globe RTS. This document is the reference specification for `ultraplan` and `ultrareview` passes.

Sections are stable-numbered. Cross-phase references use `§N`. Ambiguities that must be resolved before implementation are marked `SPEC_AMBIGUOUS-NN` and indexed in §34.

---

## §1 Intent

Replace the 2D Mercator play surface with a geometrically correct sphere. Missiles arc over the poles on great circles. Bombers fly under the radar horizon. The camera orbits the earth instead of panning across a flat quad. Gameplay identity — ten-nation nuclear brinkmanship, escalating DEFCON, alliance/diplomacy, vector-era aesthetic — is preserved.

## §2 Scope

Phases 0–5 (§26–§31). Gate criteria per phase; every gate is independently shippable.

Source baseline: the official v1.6 release at `C:/DEFCON/`. Target build: `targets/msvc/DefconX.sln`, configuration `Release | Win32`, Visual Studio 2010, DirectX 9 SDK. macOS/Linux parity deferred to §31.

## §3 Non-goals

- No engine swap. SDL 1.2 + DirectX 9 + in-house Eclipse/Gucci/Tosser/NetLib stack stays.
- No modern graphics. Silhouette/icon vector aesthetic preserved. No PBR, no 3D unit models in phases 0–4.
- No rules rewrite. Unit kinds, DEFCON gating, alliance mechanics, victory conditions unchanged.
- No language/toolchain modernization. Pre-C++11 idioms of the original preserved. Hungarian prefixes, raw pointers, no namespaces.
- No save-file migration. Clean break (§25).

## §4 Invariants (must hold at every gate)

- **I-1 Fixed-point simulation.** All authoritative state uses `Fixed` (see [source/lib/math/](../source/lib/) hierarchy and [source/world/worldobject.h](../source/world/worldobject.h)). Floats appear only in the renderer.
- **I-2 10 Hz server tick.** `SERVER_ADVANCE_FREQ` in [source/app/globals.h](../source/app/globals.h). Client prediction/interpolation is the only framerate-dependent code.
- **I-3 Lon/lat authoritative.** Position state serialized as `(longitude, latitude, altitude)`; Cartesian XYZ is derived per-tick and never the source of truth. Preserves save/protocol shape.
- **I-4 Cross-platform determinism.** Two sim runs on Win/macOS/Linux with identical seed produce bit-identical state-hash traces at every tick boundary. CI-enforced.
- **I-5 Code style continuity.** Original conventions retained so diffs remain readable against upstream. No incidental modernization.
- **I-6 Versioned protocol breaks.** Network protocol revisions bump a single version integer; no mixed-version matchmaking.

## §5 Glossary

- **Great circle** — shortest path between two points on a sphere; a geodesic on a sphere.
- **Rhumb line** — constant-bearing path; straight on Mercator, not shortest on a sphere.
- **Apogee** — highest altitude of a ballistic arc.
- **ECEF** — Earth-Centered, Earth-Fixed Cartesian (x,y,z with earth center as origin).
- **HEALPix** — Hierarchical Equal Area isoLatitude Pixelization; sphere discretization.
- **Antimeridian / seam** — the ±180° longitude line, artifact of 2D map; vanishes on the sphere.
- **Tick** — one server-advance step at 10 Hz.
- **Fix trig** — `sin`/`cos`/`atan2`/`sqrt` implemented in the `Fixed` domain, bit-identical cross-platform.

---

# Subsystem specifications

## §6 Coordinate model

State: `Fixed m_longitude, m_latitude, m_altitude` on `WorldObject`. Altitude added; existing fields preserved. Units:

- Longitude: degrees, range `[-180, 180)`.
- Latitude: degrees, range `[-90, 90]`.
- Altitude: **SPEC_AMBIGUOUS-01** — choose one of `{meters as Fixed, kilometers as Fixed, normalized where earth_radius = 1}`. Recommendation: meters, to keep radar-horizon and ballistic physics in natural units. Consequence pushes through §7–§13, §22.

Earth radius: **SPEC_AMBIGUOUS-02** — `{6'371'000 m (mean), 6'378'137 m (WGS84 equatorial), 1.0 normalized}`. The game does not need WGS84 fidelity; a mean sphere is sufficient and keeps math cheaper.

Derived representation: ECEF `Vector3<Fixed>` computed by `LonLatAltToECEF` per tick where needed; never stored. Existing `WorldObject::m_vel` is already `Vector3<Fixed>`; its semantic shifts from "tangent-plane velocity with z≈0" to "ECEF velocity" (§10).

## §7 Fixed-point math & trig

Required primitives in `lib/math/`:

- `Fixed::sin`, `cos`, `tan`, `asin`, `acos`, `atan2`, `sqrt`, `hypot`.
- Deterministic across MSVC/Clang/GCC. Table-based or CORDIC, not platform `libm`.

Audit existing `Fixed` trig coverage first. **SPEC_AMBIGUOUS-03** — implementation strategy: `{CORDIC, lookup table + linear interp, Taylor series with range reduction}`. CORDIC is usually best for determinism/precision trade-off; lookup is faster if accuracy budget permits.

Precision budget: **SPEC_AMBIGUOUS-04** — maximum cross-platform deviation at the `Fixed` level, expressed in ULPs, that still guarantees identical gameplay outcomes. Needs a drift-amplification test to pick.

## §8 Spherical geometry primitives

New `lib/math/sphere.h`:

- `GreatCircleDistance(a, b) -> Fixed` (haversine in Fixed).
- `GreatCircleBearing(a, b) -> Fixed` (initial bearing).
- `GreatCircleInterpolate(a, b, t) -> LLA` (slerp on sphere; t in [0,1]).
- `GreatCircleDestination(origin, bearing, distance) -> LLA` (forward azimuth).
- `LonLatAltToECEF(lla) -> Vector3<Fixed>`.
- `ECEFToLonLatAlt(v) -> LLA`.
- `RaySphereIntersect(origin, dir, radius) -> {hit, t0, t1}` (for picking, §15).

**SPEC_AMBIGUOUS-05** — haversine vs. Vincenty-like formula. Haversine is cheap and accurate for a sphere (we're not on an ellipsoid, §6). Use haversine.

## §9 Altitude system

Altitude domains per unit type:

| Unit | Altitude domain | Behavior |
|---|---|---|
| City, Silo, RadarStation, AirBase | `alt = 0` | Static, surface. |
| Sub (surface) | `alt = 0` | Visible on radar. |
| Sub (submerged) | `alt < 0` (negative) | Hidden from most radar. |
| Carrier, Battleship | `alt = 0` | Surface naval. |
| Fighter | bounded band, e.g. `[5 km, 15 km]` | Intercepts, escorts. |
| Bomber | bounded band, e.g. `[8 km, 12 km]` | Low-altitude penetration possible. |
| Nuke (ballistic) | arc `[0, apogee]`, apogee `~300–1200 km` depending on range | §11. |
| Tornado, Saucer | unchanged or surface-bound | Low priority. |

**SPEC_AMBIGUOUS-06** — should sub depth be continuous (player-controlled dive depth) or binary (surface/submerged)? Original is binary-ish. Recommend preserving binary unless a gameplay reason emerges; cheaper in UI and network.

**SPEC_AMBIGUOUS-07** — should bomber altitude be player-controlled (like dive depth) or constant? Controlling it opens "low under radar" tactics, which is thematic; complexity cost is one extra action slot and network field.

**SPEC_AMBIGUOUS-08** — altitude serialization precision. Full `Fixed` (heavy bandwidth) vs. quantized (e.g. 16-bit stepped). Affects §22.

## §10 Movement kinematics

- Surface units (sub, ship, carrier): great-circle motion along bearing toward target. Speed in Fixed m/s or km/tick (**SPEC_AMBIGUOUS-09**, tied to §6 unit choice).
- Aircraft: great-circle motion with altitude band held (Phase 2). Turning radius preserved.
- Position integration per tick: `pos_{t+1} = GreatCircleDestination(pos_t, bearing, speed * dt)`.

Deletions:
- [`MovingObject::CrossSeam`](../source/world/movingobject.h) — obsolete, sphere has no seam.
- `m_targetLongitudeAcrossSeam`, `m_targetLatitudeAcrossSeam` — obsolete.
- All lon-wrap special cases in [source/renderer/map_renderer.h](../source/renderer/map_renderer.h) — obsolete in globe renderer.

**SPEC_AMBIGUOUS-10** — great-circle path at/near a pole has a singularity (bearing undefined). Fallback policy: `{forbid targets within ε of pole, snap to ε-boundary, switch to ECEF integration in polar cap}`. Snap is simplest.

## §11 Ballistic missile trajectory

Current behavior: [`Nuke`](../source/world/nuke.h) uses `m_curveDirection` + total distance to fake a 2D arc.

Target behavior: real sub-orbital arc. The geodesic ground track is a great circle (§8). Altitude profile is an apogee curve parameterized by range.

Model: **SPEC_AMBIGUOUS-11** — choose one of:
- (A) **Geometric arc.** Ground track is great-circle `s`; altitude is `h(s) = h_max · sin(π s / S)` where `S` is total ground-track length. Cheap, deterministic, not physically motivated. Easiest in `Fixed`.
- (B) **Closed-form Kepler.** Solve the elliptical orbit with two endpoints and launch energy; evaluate position as a function of tick. Physically motivated, bounded complexity, but requires Kepler solver in `Fixed`.
- (C) **Numerical integration.** RK4 in ECEF with central gravity. Deterministic if step size & Fixed trig are pinned, but integration-error drift over hundreds of ticks is a determinism risk.

Recommendation: (A) for Phase 2, (B) as Phase 4 upgrade if desired. Never (C).

Apogee calibration: **SPEC_AMBIGUOUS-12** — apogee as a function of range. A reasonable table: 0 km → 0, 1000 km → ~300 km, 5000 km → ~800 km, 10000 km → ~1200 km, saturate. Must be monotonic and deterministic.

MIRV split: **SPEC_AMBIGUOUS-13** — split altitude `{fixed at apogee, configurable per-warhead, player-specified}`. Apogee is the canonical answer.

Interception geometry: **SPEC_AMBIGUOUS-14** — fighter ABM interception — must the interceptor reach the nuke's altitude, or is it a 2D projection check like today? True 3D interception is thematic and enables interception windows; preserves design if we commit to altitude meaning something.

## §12 Radar coverage

Current: [`RadarGrid`](../source/world/radargrid.h) is a `360×200` lat/lon grid storing per-team coverage counts.

Three-way tradeoff for the spherical rework:

| Option | Pros | Cons |
|---|---|---|
| (α) Keep flat lat/lon grid, correct cell weights for area | Minimal code change | Pole distortion remains |
| (β) HEALPix (equal-area) | Uniform resolution on sphere | New indexing, higher code cost |
| (γ) Icosphere / geodesic grid | Equal area, natural for visualization | Neighbor lookup complexity |

**SPEC_AMBIGUOUS-15** — pick one. Recommend (α) in Phase 1 (fastest path), optionally migrate to (β) in Phase 4 if profiling justifies.

Coverage radius on a sphere: existing `Fixed _radius` becomes a great-circle arc distance. Update `AddCoverage`/`RemoveCoverage`/`UpdateCoverage`/`GetCoverage` to iterate cells by spherical distance rather than Euclidean.

## §13 Line-of-sight & horizon

New: radar horizon as a function of altitude.

- Ground-based radar (alt ≈ 0, tower h_r): horizon distance ≈ `sqrt(2·R·h_r)`.
- Target at altitude h_t visible up to `sqrt(2·R·h_r) + sqrt(2·R·h_t)`.
- In Fixed: `HorizonArc(h_r, h_t) -> Fixed`, returning arc distance.

**SPEC_AMBIGUOUS-16** — refraction term. Real atmospheric refraction extends radar horizon ~15%. Include as a single tunable constant or omit. Recommend omit for Phase 2, add as `k_refraction` constant in Phase 4 polish.

Sub detection: depth reduces detection chance; current model preserved with altitude substitution.

Occlusion by earth itself: for space-based or very high targets, line-of-sight through the earth is blocked. `LineOfSight(a, b) -> bool` via ray-vs-sphere (§8).

## §14 Camera system

Orbit camera, spherical coordinates:

- `m_camYaw` — longitude of camera sub-point.
- `m_camPitch` — latitude of camera sub-point.
- `m_camDistance` — altitude above surface.
- `m_camRoll` — usually 0; exposed for cinematics only.

View matrix: look-at from `(yaw, pitch, distance)` toward earth center.

Controls:
- Drag: rotate in yaw/pitch.
- Wheel: distance (zoom).
- Pitch clamped to `[-89°, 89°]` to avoid gimbal issues.

**SPEC_AMBIGUOUS-17** — zoom-to-tactical transition. Options: `{smooth continuous, discrete "global"/"theater"/"local" modes with animated interpolation, hybrid}`. Smooth is simpler; discrete modes can carry visual affordance changes (switch to labeled theater overlays at theater scope).

**SPEC_AMBIGUOUS-18** — drag semantics. `{trackball (grab world), turntable (grab camera)}`. Trackball is more natural for a globe; turntable is cheaper.

**SPEC_AMBIGUOUS-19** — camera auto-follow in events (e.g. nuke impact alerts). Original has `CenterViewport`. Port to orbit: center means reposition sub-point to target, animate over `camSpeed` ticks.

## §15 Picking & selection

Replace [`MapRenderer::ConvertPixelsToAngle`](../source/renderer/map_renderer.h) with ray-vs-sphere:

1. Unproject mouse pixel to a ray in world space.
2. Intersect with earth sphere (§8).
3. If hit, near-hit point is the pick location; convert ECEF → lon/lat.
4. If no hit, pick space.

For unit selection: each object has a selection sphere at its ECEF position with radius scaled to screen size; ray-vs-sphere per object, keep nearest front-face hit.

**SPEC_AMBIGUOUS-20** — back-of-globe picking. Should objects on the far side be selectable or culled? Recommend cull; forcing the player to rotate the globe is clearer.

## §16 Globe renderer

New `source/renderer/globe_renderer.{h,cpp}`, promoted from techniques in [`LobbyRenderer::RenderGlobe`](../source/renderer/lobby_renderer.h) + `SetupCamera3d`.

Pipeline:
1. Clear, set 3D camera (§14).
2. Render sphere — solid filled water color.
3. Render coastlines by projecting [`EarthData::m_islands`](../source/world/earthdata.h) polyline vertices to sphere surface.
4. Render borders similarly.
5. Render entities: for each `WorldObject`, compute ECEF, project to screen, rasterize sprite or icon. Back-face cull via dot(normal, view_dir).
6. Render trails: great-circle polylines instead of 2D polylines.
7. Render radar overlay (§12).
8. Render explosions as billboards aligned to surface normal.

Existing `EarthData` island polylines are already `Vector3<float>`; interpret their XY as lon/lat, project.

**SPEC_AMBIGUOUS-21** — sphere tessellation. Icosphere subdivision level or UV sphere latitude/longitude steps. Must balance silhouette smoothness and fill cost. Start with subdivision-3 icosphere (~642 tris).

**SPEC_AMBIGUOUS-22** — coastline rendering. `{draw as spherical polylines on top of sphere, bake into a 2048² texture wrapped on sphere, both}`. Polylines preserve vector aesthetic; texture is faster. Recommend polylines in Phase 0, texture option in Phase 4.

**SPEC_AMBIGUOUS-23** — back-face culling policy for entity icons on the far side: `{hard cull, fade with 1-dot factor}`. Fade is prettier but costs a pass; hard cull is fine for Phase 0.

## §17 UI overlay projection

All screen-space UI that anchors to world positions needs a `WorldToScreen(lla)` helper. Returns `{onScreen: bool, pixel: Vec2, behindGlobe: bool}`. Call sites in [source/interface/](../source/interface/): tooltips, action menus, unit callouts, placement icons, nuke authorization dialog.

**SPEC_AMBIGUOUS-24** — label behavior when unit is behind globe. `{hide, show with dashed leader line from visible edge, show at clipped edge}`. Hide is simplest.

## §18 Fleet navigation

Existing: a 2D navigability bitmap `bmpSailableWater` encodes which water cells are sailable.

Path search: currently 2D A* (implicit in engine). On the sphere, A* becomes:
- Neighbor step by fixed great-circle arc length.
- Bitmap remains addressable by (lon, lat) — equirectangular texture — so lookup cost stays O(1).
- Heuristic becomes great-circle distance.

**SPEC_AMBIGUOUS-25** — pole handling in pathing. Bitmap rows near poles collapse to sub-cell sizes; paths may degenerate. Recommend forbidding pathing within ε-cap of each pole.

## §19 Territory & placement

Existing: six hardcoded territories (`World::Territory_NorthAmerica`, etc.) with polygon bounds used for placement rules.

Point-in-polygon on a sphere: implement `SphericalPointInPolygon(p, polygon) -> bool` using sum-of-signed-areas or ray-casting along a meridian. Must be robust at antimeridian and poles.

**SPEC_AMBIGUOUS-26** — territory polygon data format. Keep existing 2D polygons reinterpreted as spherical, or re-author polygons for sphere correctness. Reinterpretation is cheaper; edge cases near antimeridian must be split.

## §20 Whiteboard

Current: screen-space polyline strokes drawn on the flat map ([source/world/whiteboard.h](../source/world/whiteboard.h), [source/interface/whiteboard_panel.h](../source/interface/whiteboard_panel.h)).

On a sphere, polylines must live on the surface. Options:

**SPEC_AMBIGUOUS-27** — whiteboard representation:
- (A) Geodesic polyline — list of `(lon, lat)` points, rendered as great-circle arcs between consecutive points.
- (B) Surface texture — paint into an equirectangular texture wrapped on sphere.
- (C) 3D decal — project strokes onto sphere as mesh decals.

Recommend (A). Keeps data model simple, preserves network payload shape, works with existing stroke serialization.

## §21 AI adjustments

AI evaluation functions in [`Team`](../source/world/team.h) and derived targeting logic use 2D distances. Replace with great-circle for all range tests. Review and retune:

- Territory scouting patterns (formerly rectangular sweeps in 2D; now sweep-along-parallel or spiral-from-pole).
- Nuke target prioritization (uses range now spherical).
- Fleet dispatch ranges.
- Aggression timing constants may not need changes.

**SPEC_AMBIGUOUS-28** — should AI exploit altitude (low-alt bomber paths, depressed-trajectory nukes)? If bomber altitude is player-controllable (§9 SPEC_AMBIGUOUS-07), AI needs an equivalent policy. Otherwise altitude is a constant and AI logic barely changes.

## §22 Network protocol

Add altitude to position transmissions. Affected commands in [network_defines.h](../source/network/network_defines.h): object-state updates, movement orders, waypoint placements, action targeting, whiteboard strokes.

Wire format: `(longitude: Fixed, latitude: Fixed, altitude: Fixed)` where altitude uses the quantization from §9 SPEC_AMBIGUOUS-08.

**SPEC_AMBIGUOUS-29** — protocol version field strategy. Existing scheme uses an implicit version; make it explicit. Integer `PROTOCOL_VERSION` bumped per breaking change. Clients with mismatch refuse connection with a clear diagnostic.

Bandwidth impact: per-unit position payload grows by one `Fixed`. Estimate cost and decide if quantization is needed — tie to §9 SPEC_AMBIGUOUS-08.

## §23 Client prediction & interpolation

Existing client-side prediction extrapolates position linearly in lon/lat. Replace with:

- Surface units: predict along current bearing (great-circle extrapolation).
- Aircraft/nukes: predict along current velocity vector in ECEF; reproject to lon/lat/alt.

**SPEC_AMBIGUOUS-30** — interpolation basis: `{LLA slerp, ECEF lerp + renormalize, great-circle interp for surface + ECEF interp for airborne}`. Hybrid matches physics.

## §24 Determinism harness

Required in Phase 0. Without this, no other phase is trustworthy.

Components:
- **Headless sim driver** — runs `World::Update` N ticks without renderer.
- **State hash** — canonical serialization of `World` → SHA1 per tick.
- **Golden trace** — pinned Win32 reference trace of state hashes for a seeded scenario.
- **CI matrix** — Windows (MSVC), macOS (Clang), Linux (GCC). Trace must match byte-for-byte.
- **Drift localizer** — when divergence occurs, binary-search to the first mismatched tick and dump struct diffs.

**SPEC_AMBIGUOUS-31** — scenario count. One seeded scenario is enough for Phase 0. Expand to three covering: pure surface movement, nuke exchange with MIRV, radar-heavy mid-game. Recommend three from the start.

**SPEC_AMBIGUOUS-32** — CI vendor. Local batch script vs. GitHub Actions vs. Windows-only locally + Linux in CI. Recommend GitHub Actions for all three OSes; matches project tooling expectations.

## §25 Save / replay / mod compatibility

- **Save format**: breaks at Phase 2. Version field added to save header; older versions refused.
- **Replay format**: breaks with protocol (§22). Replays authored after Phase 2 incompatible with pre-Phase 2 binaries.
- **Mod format**: breaks at Phase 0. Maps authored for 2D equirectangular projection are semantically meaningless on the sphere; refuse to load with a clear diagnostic. Asset mods (sounds, icons) unaffected.

**SPEC_AMBIGUOUS-33** — mod compatibility shim for asset-only mods. Distinguish asset-only from map mods and allow the former. Needs mod manifest extension.

---

# Phase plan

Each phase ends at a gate. The gate criteria are binary. A failed gate blocks the next phase.

## §26 Phase 0 — Rendering-only globe

**Goal.** Visual 3D globe runs as an alternative view over unchanged simulation. Determinism harness green. Project on GitHub, CI wired.

**Scope.**
- New `source/renderer/globe_renderer.{h,cpp}`, instantiated alongside `MapRenderer`.
- Read-only view of existing 2D game state; positions projected to sphere surface (alt = 0 everywhere).
- Orbit camera with drag + zoom.
- Ray-vs-sphere picking returning `(lon, lat)` compatible with existing selection code path.
- Toggle key to switch between `MapRenderer` and `GlobeRenderer`.
- Determinism harness (§24) committed and CI-green on Win/macOS/Linux for three scenarios.
- Project pushed to GitHub; CI badge green.

**Out of scope for Phase 0.**
- Altitude (still 0).
- Great-circle movement (still 2D interp under the hood).
- Gameplay behavior changes.
- UI overlay reprojection (keep the old UI attached to the 2D renderer; when globe view is active, screen-space UI anchored to units may mis-align — acceptable for Phase 0).

**Gate criteria.**
- G0.1 Both renderers selectable at runtime; no regressions in 2D view.
- G0.2 Globe renders coastlines, borders, cities, units at correct lon/lat in 3D.
- G0.3 Orbit camera and ray picking functional; mouse-click selects correct unit.
- G0.4 Determinism CI green across 3 OSes × 3 scenarios × 10000 ticks.
- G0.5 Release build compiles warning-free under VS2010.

## §27 Phase 1 — Spherical distance & paths

**Goal.** Simulation understands the sphere. Movement is great-circle. All range tests use great-circle distance. Seam machinery deleted.

**Scope.**
- `lib/math/sphere.h` primitives (§8).
- `Fixed` trig audit and completion (§7).
- All `distance_sq` / Euclidean range tests in world code replaced with great-circle or great-circle-squared.
- `MovingObject` integration switched to `GreatCircleDestination`.
- `CrossSeam`, `m_targetLongitudeAcrossSeam`, `m_targetLatitudeAcrossSeam`, related rendering special cases — deleted.
- Nuke ground track becomes a great circle. No altitude yet (apogee stubbed to 0).
- Radar coverage distance uses great-circle (data structure still flat grid; §12 option α).
- Determinism harness updated with post-Phase 1 golden trace.

**Out of scope.**
- Altitude, ballistic arcs, radar horizon.
- Camera/UI polish.
- Network protocol change.

**Gate criteria.**
- G1.1 A nuke launched from Moscow targeting NYC follows a great-circle ground track (arcs over the Arctic), visible in the globe renderer.
- G1.2 No code path references `CrossSeam` or antimeridian special cases.
- G1.3 Two-client match plays to completion with identical state hashes across three OSes.
- G1.4 AI win rate in a reference mirror-match scenario within ±5% of Phase 0 baseline (sanity check; nothing tuned yet).
- G1.5 Determinism CI green.

## §28 Phase 2 — Altitude

**Goal.** The third dimension becomes real. Bombers fly at altitude. Nukes have apogee. Radar has a horizon. Network protocol bumped.

**Scope.**
- `m_altitude` added to `WorldObject` and propagated through movement, rendering, network.
- Altitude bands enforced per §9.
- Ballistic trajectory per §11 option (A).
- Radar horizon per §13.
- Network protocol version bump (§22); save-version bump (§25).
- Client prediction extended per §23.
- Fighter ABM interception extended to 3D per §11 SPEC_AMBIGUOUS-14.
- Determinism harness updated with new golden trace including nuke/MIRV scenario.

**Out of scope.**
- Continuous sub depth or player-controlled bomber altitude (deferred unless SPEC_AMBIGUOUS-06/07 resolve otherwise).
- Day/night, atmosphere.

**Gate criteria.**
- G2.1 Deliverable: playable 3D-globe DEFCON match between two clients, cross-platform determinism green, altitude-aware radar, great-circle + altitude missile trajectories.
- G2.2 A bomber at low altitude evades a ground-based radar beyond its horizon; the same bomber at standard altitude does not.
- G2.3 A MIRV missile splits at apogee and impacts multiple targets with correct ground tracks.
- G2.4 Protocol version check refuses mismatched clients with user-readable error.
- G2.5 Save/load round-trip preserves all altitude data; pre-Phase-2 saves rejected with diagnostic.
- G2.6 Determinism CI green.

## §29 Phase 3 — Camera & UI polish

Orbit controls refined (§14 SPEC_AMBIGUOUS-17, -18, -19). UI overlay projection fully rewired (§17). Label occlusion handled. Placement icons, action lines, tooltips, nuke-authorization UI all correct in 3D.

Gate: No UI anchored to a world position mis-renders in globe view.

## §30 Phase 4 — Polish

Day/night terminator. Optional atmosphere scattering. Whiteboard redesign per §20. Optional HEALPix radar (§12 option β) if profiled. Optional Keplerian nuke trajectory (§11 option B).

Gate: Shipping-quality presentation.

## §31 Phase 5 — AI, mods, platforms

AI retuning per §21. Mod format finalization and shim per §25 SPEC_AMBIGUOUS-33. macOS and Linux build parity restored (they break somewhere between Phase 0 and Phase 4 without active maintenance).

Gate: Full tri-platform release-ready build.

---

# Appendices

## §32 Risk register

- **R-1** `Fixed` trig determinism. If current trig isn't bit-identical across compilers, Phase 1 is impossible without first implementing it. Audit in Phase 0.
- **R-2** Great-circle integration drift. Successive destination calls can accumulate error. Verify closed-form evaluation from fixed origin beats step-integration for long paths.
- **R-3** Renderer performance. Globe with polyline coastlines + per-unit picking can be slower than `MapRenderer`. Budget: match 2D FPS ±10%.
- **R-4** UI regression cascade. Every anchored UI element is a latent breakage. Reserve Phase 3 explicitly for this rather than folding into Phase 0.
- **R-5** Save/replay orphaning. Communicate break clearly; no silent reinterpretation of old saves.
- **R-6** Mod community break. Asset-only mods still work; map mods don't. Communicate and provide the shim (SPEC_AMBIGUOUS-33).

## §33 File impact register

Create:
- `source/renderer/globe_renderer.{h,cpp}` — §16.
- `source/lib/math/sphere.{h,cpp}` — §8.
- `source/lib/math/fixed_trig.{h,cpp}` (if not present) — §7.
- `source/test/determinism_harness.{h,cpp}` — §24.

Modify:
- [source/world/worldobject.h](../source/world/worldobject.h) — add `m_altitude` (Phase 2).
- [source/world/movingobject.h](../source/world/movingobject.h) + `.cpp` — replace kinematics, delete `CrossSeam` (Phase 1).
- [source/world/nuke.h](../source/world/nuke.h) + `.cpp` — ballistic arc (Phase 1 ground track, Phase 2 altitude).
- [source/world/radargrid.h](../source/world/radargrid.h) + `.cpp` — spherical coverage (Phase 1) + horizon (Phase 2).
- [source/world/team.h](../source/world/team.h) + `.cpp` — AI range tests, targeting (Phase 1, tune Phase 5).
- [source/renderer/map_renderer.h](../source/renderer/map_renderer.h) + `.cpp` — coexist with globe renderer, toggle; delete seam-handling (Phase 1).
- [source/interface/](../source/interface/) — overlay projection (Phase 3).
- [source/network/network_defines.h](../source/network/network_defines.h) + related — protocol bump (Phase 2).
- [source/app/app.h](../source/app/app.h) + `.cpp` — wire determinism harness, globe renderer.

Delete:
- `CrossSeam` machinery across `MovingObject`, `MapRenderer`, `World` as applicable.

## §34 SPEC_AMBIGUOUS index

Each item must be resolved before the phase referenced in its "Blocks" column begins. `ultraplan` is expected to propose and justify a resolution per ambiguity.

| ID | Topic | §ref | Blocks |
|---|---|---|---|
| 01 | Altitude unit | §6 | Phase 2 |
| 02 | Earth radius constant | §6 | Phase 1 |
| 03 | Fixed trig implementation strategy | §7 | Phase 1 |
| 04 | Fixed trig precision budget | §7 | Phase 1 |
| 05 | Haversine vs. Vincenty | §8 | Phase 1 |
| 06 | Sub depth continuous vs. binary | §9 | Phase 2 |
| 07 | Bomber altitude player-controlled vs. constant | §9 | Phase 2 |
| 08 | Altitude serialization quantization | §9, §22 | Phase 2 |
| 09 | Speed units | §10 | Phase 1 |
| 10 | Pole singularity policy | §10 | Phase 1 |
| 11 | Ballistic trajectory model (A/B/C) | §11 | Phase 2 |
| 12 | Apogee vs. range calibration table | §11 | Phase 2 |
| 13 | MIRV split altitude policy | §11 | Phase 2 |
| 14 | ABM interception geometry (2D vs. 3D) | §11 | Phase 2 |
| 15 | Radar grid data structure (α/β/γ) | §12 | Phase 1 |
| 16 | Atmospheric refraction in radar horizon | §13 | Phase 2 |
| 17 | Zoom-to-tactical transition mode | §14 | Phase 3 |
| 18 | Drag semantics (trackball/turntable) | §14 | Phase 0 |
| 19 | Camera auto-follow port | §14 | Phase 3 |
| 20 | Back-of-globe picking policy | §15 | Phase 0 |
| 21 | Sphere tessellation level | §16 | Phase 0 |
| 22 | Coastline rendering technique | §16 | Phase 0 |
| 23 | Back-face entity cull/fade | §16 | Phase 0 |
| 24 | Behind-globe label policy | §17 | Phase 3 |
| 25 | Pathing pole handling | §18 | Phase 1 |
| 26 | Territory polygon reinterpret vs. re-author | §19 | Phase 1 |
| 27 | Whiteboard representation | §20 | Phase 4 |
| 28 | AI altitude exploitation | §21 | Phase 2 |
| 29 | Protocol version scheme | §22 | Phase 2 |
| 30 | Client prediction interpolation basis | §23 | Phase 2 |
| 31 | Determinism scenario count | §24 | Phase 0 |
| 32 | CI vendor | §24 | Phase 0 |
| 33 | Mod asset-vs-map shim | §25 | Phase 5 |

33 ambiguities. Every resolution must preserve the invariants in §4.
