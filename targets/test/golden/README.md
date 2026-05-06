# Golden determinism traces

Pinned trace files used by `.github/workflows/determinism.yml` to enforce
invariant I-4 (cross-OS bit-identity) at every phase gate.

| File | Scenario | Description |
|---|---|---|
| `surface.trace` | surface | Four units stepping along great-circles. Exercises sphere primitives + Fixed trig. |
| `mirv.trace` | mirv | One nuke launching with apogee profile. Exercises sphere + Nuke apogee table + altitude integration. |
| `radar.trace` | radar | Six bombers at 10 km on varied paths (anti-meridian, pole-to-pole, antipodal). Stress test for sphere primitives. |

Each line is `<tick>\t<fnv1a-64-hex>\n`. CI matrix
{Win MSVC/MinGW, macOS Clang, Linux GCC} runs `determinism_test` for
each scenario, 10000 ticks, then `diff -q` against the golden file.

Re-baselining policy: when an intentional change to the harness or to
the primitives it exercises lands, regenerate via:

```
cd targets/test/determinism_make && make
./determinism_test surface 10000 ../golden/surface.trace
./determinism_test mirv    10000 ../golden/mirv.trace
./determinism_test radar   10000 ../golden/radar.trace
```

Re-baselining must happen on the same platform that the prior baseline
was captured on (currently Linux x86-64), then propagated via PR with
reviewer sign-off.

The driver source is `source/test/determinism_main.cpp`; it does *not*
launch DEFCON (that path is graphics/audio/network-coupled). Adding
the rest of `World::Update` under the same hash is a Phase 1
follow-up once a true headless mode lands.
