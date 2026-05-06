#include "lib/universal_include.h"

#include <stdio.h>
#include <string.h>

#include "lib/math/fixed.h"
#include "lib/math/vector3.h"
#include "lib/tosser/bounded_array.h"

#include "world/world.h"
#include "world/worldobject.h"
#include "world/date.h"
#include "world/radargrid.h"

#include "test/determinism_harness.h"


//
// Determinism harness implementation.  See determinism_harness.h for
// rationale.  Everything here must be deterministic byte-for-byte across
// {Win MSVC, macOS Clang, Linux GCC} - that is the entire point.
//


static FILE *s_traceFp = NULL;


unsigned long long DeterminismHarness::FnvUpdateBytes( unsigned long long h,
                                                       const void *data, int len )
{
    const unsigned char *b = (const unsigned char *) data;
    for( int i = 0; i < len; ++i )
    {
        h ^= (unsigned long long) b[i];
        h *= FNV_PRIME;
    }
    return h;
}


//
// Helper: hash a Fixed by its raw m_value.  We rely on Fixed64 having a
// SInt64 m_value member; the harness lives in the same translation unit
// flag as the rest of the engine so the storage layout is fixed.
//
// We reach into Fixed by reinterpreting bytes - but this requires the
// same in-memory representation across compilers, which is exactly
// invariant I-1.
//
static unsigned long long HashFixed( unsigned long long h, const Fixed &f )
{
    // m_value is private; pull it via a trivially-bitwise copy of the
    // object.  sizeof(Fixed) == sizeof(SInt64) for the FIXED64_NUMERICS
    // build (one m_value member).  For FLOAT_NUMERICS the layout differs
    // and the harness may report different traces - acceptable: float
    // numerics is not the production path.
    return DeterminismHarness::FnvUpdateBytes( h, &f, sizeof(Fixed) );
}


static unsigned long long HashInt( unsigned long long h, int v )
{
    return DeterminismHarness::FnvUpdateBytes( h, &v, sizeof(int) );
}


//
// Object-state hash.  Order is fixed so the trace is reproducible.
//
static unsigned long long HashObject( unsigned long long h, WorldObject *obj )
{
    h = HashInt( h, obj->m_teamId );
    h = HashInt( h, obj->m_objectId );
    h = HashInt( h, obj->m_type );
    h = HashFixed( h, obj->m_longitude );
    h = HashFixed( h, obj->m_latitude );
    // Phase 2: m_altitude folded into the trace.  Diverges from the
    // Phase 0 / Phase 1 golden traces - intentional versioned break.
    h = HashFixed( h, obj->m_altitude );
    h = HashFixed( h, obj->m_vel.x );
    h = HashFixed( h, obj->m_vel.y );
    h = HashFixed( h, obj->m_vel.z );
    h = HashInt( h, obj->m_life );
    h = HashInt( h, obj->m_currentState );
    h = HashFixed( h, obj->m_stateTimer );
    return h;
}


unsigned long long DeterminismHarness::ComputeWorldHash( World *world )
{
    if( !world ) return 0ULL;

    unsigned long long h = FNV_OFFSET;

    //
    // World date.
    //
    h = HashFixed( h, world->m_theDate.m_theDate );

    //
    // Per-object state, sorted by m_objectId for determinism across
    // any container reorderings.
    //
    int n = world->m_objects.Size();
    int *order = new int[n + 1];
    int orderCount = 0;
    for( int i = 0; i < n; ++i )
    {
        if( world->m_objects.ValidIndex( i ) )
        {
            order[orderCount++] = i;
        }
    }
    // Insertion sort by m_objectId (n is small enough; avoids stdlib qsort
    // determinism caveats across compilers).
    for( int i = 1; i < orderCount; ++i )
    {
        int x = order[i];
        int xId = world->m_objects[x]->m_objectId;
        int j = i;
        while( j > 0 && world->m_objects[ order[j-1] ]->m_objectId > xId )
        {
            order[j] = order[j-1];
            --j;
        }
        order[j] = x;
    }
    for( int i = 0; i < orderCount; ++i )
    {
        h = HashObject( h, world->m_objects[ order[i] ] );
    }
    delete [] order;

    //
    // Radar grid.  Hash row-major; per-cell hash the per-team coverage
    // counts.  RadarGrid does not expose its internals publicly so we
    // hash via a friend hook added below; if/when the exposure changes
    // this code adapts.  For now this is a placeholder: hash a constant
    // so the function returns deterministically until the friend hook
    // lands in radargrid.h.  (TODO: Phase 1 wires the real radar bytes.)
    //
    h = FnvUpdateBytes( h, "radar-stub", 10 );

    return h;
}


void DeterminismHarness::WriteTraceLine( FILE *fp, int tick, unsigned long long hash )
{
    if( !fp ) return;
    fprintf( fp, "%d\t%016llx\n", tick, hash );
}


static int s_tickCounter = 0;


void DeterminismHarness::OnTick( World *world, int tick )
{
    if( !s_traceFp ) return;
    int t = (tick >= 0) ? tick : s_tickCounter++;
    unsigned long long h = ComputeWorldHash( world );
    WriteTraceLine( s_traceFp, t, h );
}


void DeterminismHarness::InstallTraceFile( FILE *fp )
{
    s_traceFp = fp;
}


void DeterminismHarness::UninstallTraceFile()
{
    if( s_traceFp )
    {
        fflush( s_traceFp );
        s_traceFp = NULL;
    }
}
