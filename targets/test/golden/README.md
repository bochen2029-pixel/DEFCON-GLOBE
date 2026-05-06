# Golden determinism traces

Per-phase pinned trace files used by `.github/workflows/determinism.yml`.

| File | Phase | Description |
|---|---|---|
| `phase0.trace` | Phase 0 | Pre-sphere reference. Empty until headless harness mode lands. |
| `phase1.trace` | Phase 1 | After great-circle simulation rewrite. |
| `phase2.trace` | Phase 2 | After altitude added; format extended with `m_altitude` bytes. |

Each trace file is `<tick>\t<hex64>\n` per line. CI matrix
{Win MSVC, macOS Clang, Linux GCC} must produce byte-identical traces.

Re-baselining policy: when an intentional simulation change lands, the
golden trace is regenerated on the platform that already passes (Windows
first per `docs/DESIGN_v1.md` non-goals on platform parity), then
distributed via PR with reviewer sign-off.
