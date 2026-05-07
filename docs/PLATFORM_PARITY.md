# macOS / Linux Build Parity — Phase 5 follow-up

## Status

Out of scope for the current implementation pass. The Windows
`Release | Win32` target in `targets/msvc/DefconX.sln` is the source of
truth; macOS and Linux builds break somewhere between Phase 0 and now
without active maintenance (per `docs/DESIGN_v1.md` §31).

The determinism harness already builds on Linux (see
`targets/test/determinism_make/Makefile`), confirming that the
Fixed-math + sphere-primitive layer is portable. The blockers are in
the engine substrate, not the simulation.

## Known blockers

### 1. Endianness macros

`contrib/systemIV/lib/math/fixed_64.h:15-22` gates its `UInt128`
struct on `__BIG_ENDIAN__` / `__LITTLE_ENDIAN__`. Apple's SDK headers
predefine these; Linux GCC and MinGW do not.

The harness Makefile passes `-D__LITTLE_ENDIAN__`. The full DEFCON
build needs the same in the platform sub-projects, or replace the
guard with a `__BYTE_ORDER == __LITTLE_ENDIAN` (glibc) /
`__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__` (Clang/GCC) test.

### 2. Header hygiene

`fixed_64.h` was missing `Fixed asin` / `Fixed acos` declarations even
though `fixed_64.cpp` defines them; external translation units silently
fell through to `math.h`'s `double acos` via implicit conversion. Fixed
in commit `14926a1` (Phase 0.5 harness work). Other engine headers
likely have similar gaps that only surface under stricter compilers.

### 3. SDL 1.2 / OpenGL 1.x bindings

Source uses fixed-function GL (`glBegin`, `glVertex3fv`, display lists,
`gluLookAt`, `gluPerspective`). All available on Linux Mesa via
`libsdl1.2-dev` + `libgl-dev` + `libglu-dev`. macOS deprecated GL in
2018; build under XQuartz or use the OpenGL.framework legacy entry
points still works.

### 4. DirectX 9 references

`#ifdef WIN32` gates most DX9 paths; the SDL audio path is
cross-platform. Verify no inadvertent DX9-only call leaks into a
shared header that includes from a non-Win32 .cpp.

### 5. `#include` casing

The Windows filesystem is case-insensitive; sources mix
`#include "lib/Math/..."` with `#include "lib/math/..."`. Linux is
case-sensitive and these mismatches surface as missing-file errors.
`grep -rE 'include +"[^"]*[A-Z]' source/` enumerates suspects.

### 6. C-runtime gotchas

- `strcpy_s`, `sprintf_s`, `_snprintf` are MSVC-only. Replace with the
  POSIX equivalents (`snprintf`) under `#ifndef _WIN32`.
- `__forceinline` -> `inline __attribute__((always_inline))` under GCC.
- `__try` / `__except` SEH blocks are MSVC-only; wrap in `#ifdef _MSC_VER`.

### 7. Authentication / metaserver TLS

The `lib/metaserver/` and `lib/netlib/` paths use platform-specific
socket and TLS calls. Linux/macOS need POSIX socket alternatives or a
small shim layer.

## Recommended approach

1. Add a top-level `Makefile` and a CMake `CMakeLists.txt` under
   `targets/posix/`. Mirror the source-file list from
   `targets/msvc/Defcon.vcxproj` (parse via a small script; the file
   list is mechanical).
2. Wire `-D__LITTLE_ENDIAN__` for the platforms that need it.
3. Iterate: build, fix the surface error, build, repeat. The blockers
   above are most-frequent first.
4. Add a `linux-build` and `macos-build` job to
   `.github/workflows/determinism.yml` (already a no-op build step
   in the matrix; replace with the real `make`).

Owner / timeline: this is multi-day engine work, not a polish pass.
Track separately from the sphere conversion.
