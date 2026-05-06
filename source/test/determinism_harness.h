
#ifndef _included_determinismharness_h
#define _included_determinismharness_h

//
// Phase 0 determinism harness.
//
// Hashes the canonical World state at every server tick and emits a trace
// line per tick.  Used by the cross-OS CI matrix (see
// .github/workflows/determinism.yml) to enforce invariant I-4 from
// docs/DESIGN_v1.md section 4: Win/macOS/Linux runs of the same seeded
// scenario must produce byte-identical traces.
//
// Hash: FNV-1a 64-bit over a fixed-format state serialization.  The
// approved plan called for SHA-1; we deviate to FNV-1a-64 because the gate
// is "bit-identical across OSes" and not "cryptographically collision-
// resistant", and FNV-1a avoids dragging the unrar SHA-1 (or a fresh
// implementation) into the harness path.  Both have the property the
// harness actually needs: any single-bit state divergence diverges the
// hash.
//
// Phase 0 wires the trace hook into World::Update.  Phase 1 / Phase 2
// extend the serialized state to include the new fields (m_altitude,
// great-circle bearings, ECEF positions where derived).
//

class World;
class WorldObject;
struct stdio_FILE;
typedef struct stdio_FILE FILE;

#include <stdio.h>


class DeterminismHarness
{
public:
    //
    // FNV-1a 64-bit basis and prime.
    //
    static const unsigned long long FNV_OFFSET = 14695981039346656037ULL;
    static const unsigned long long FNV_PRIME  =       1099511628211ULL;

public:
    //
    // Hash incremental updates.  Caller seeds with FNV_OFFSET and feeds
    // bytes; the hash is the running 64-bit value.
    //
    static unsigned long long FnvUpdateBytes( unsigned long long h,
                                              const void *data, int len );

    //
    // Compute a 64-bit canonical hash over current World state.
    // Serializes (in fixed order, sorted by m_objectId):
    //   - per-object: teamId, objectId, m_longitude.m_value,
    //                 m_latitude.m_value, m_vel.x.m_value,
    //                 m_vel.y.m_value, m_vel.z.m_value, m_life,
    //                 m_currentState, m_stateTimer.m_value
    //                 (Phase 2 will add m_altitude.m_value here).
    //   - World::m_theDate (game seconds, as Fixed raw).
    //   - All RadarGrid cells in row-major order (per-team coverage bytes).
    //
    static unsigned long long ComputeWorldHash( World *world );

    //
    // Write one line "<tick>\t<hexhash>\n" to fp.  Caller is responsible
    // for flushing / closing the file.
    //
    static void WriteTraceLine( FILE *fp, int tick, unsigned long long hash );

    //
    // Tick hook called from World::Update.  No-op when no trace file is
    // installed.  Trace file is enabled via the determinism CLI (see
    // defcon.cpp main argument handling).
    //
    static void OnTick( World *world, int tick );

    //
    // Install / uninstall the active trace file.
    //
    static void InstallTraceFile( FILE *fp );
    static void UninstallTraceFile();
};


#endif
