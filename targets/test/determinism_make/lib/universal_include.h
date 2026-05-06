//
// Stub lib/universal_include.h for the determinism harness build.
//
// The real source/lib/universal_include.h pulls in the entire SDL +
// DirectX + Eclipse + Tosser stack via TARGET_FINAL/TARGET_DEBUG/etc.
// macro switches.  None of that is needed to test deterministic
// fixed-point math + spherical primitives across compilers, so we
// expose only the bare minimum here:
//
//   - FIXED64_NUMERICS: pin the production Fixed implementation.
//     The harness exists to verify cross-OS bit-identity of *that*
//     numerics layer, so the float-numerics build path is irrelevant.
//   - Standard C/C++ includes used by the .cpp files we compile.
//
// If a future test scenario needs more of the engine, prefer adding
// a small real-engine include over expanding this stub.
//

#ifndef INCLUDED_UNIVERSAL_INCLUDE_H
#define INCLUDED_UNIVERSAL_INCLUDE_H

#define FIXED64_NUMERICS

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

// Stubs for the engine's debug-assert macros that some files reach for.
#ifndef AppDebugAssert
#define AppDebugAssert(x)  ((void)0)
#endif
#ifndef AppAssert
#define AppAssert(x)       ((void)0)
#endif

// Stub the engine's release-assert symbol that BoundedArray and friends
// link against.  Inline so it's available wherever the header lands.
#ifdef __cplusplus
inline void AppReleaseAssertFailed(const char *, ...) {}
#endif

#endif
