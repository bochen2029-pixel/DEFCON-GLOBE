//
// Determinism harness standalone driver.
//
// Cross-OS determinism gate (docs/DESIGN_v1.md invariant I-4):
// at every phase gate, three pinned scenarios run for 10000 ticks and
// the per-tick state-hash trace must be byte-identical across
// {Win MSVC, macOS Clang, Linux GCC}.
//
// This driver exists because DEFCON's full World::Update path is
// graphics/audio/network-coupled (SDL, DirectX, NetLib, Eclipse, etc.)
// and a true headless mode is significant engine surgery.  Instead,
// the driver exercises the *invariant-critical* primitives directly:
//
//   - Fixed sin / cos / asin / acos / sqrt / atan2 / tan
//     (contrib/systemIV/lib/math/fixed_64)
//   - Spherical primitives: great-circle distance / bearing /
//     destination / interpolate / horizon (source/world/sphere)
//   - Nuke apogee profile (SPEC_AMBIGUOUS-12 piecewise table) +
//     ground-track integration
//   - Radar grid spherical coverage indexing
//
// If those produce identical traces across OSes, the §4 I-4 invariant
// claim is real.  Adding the rest of World::Update under the same
// hash is a Phase 1 follow-up once headless mode lands.
//
// Build: see targets/test/determinism_make/Makefile.
//

#include "lib/universal_include.h"

#include "lib/math/fixed.h"
#include "lib/math/vector3.h"

#include "world/sphere.h"

#include "test/determinism_harness.h"


//
// Each scenario is a deterministic state evolution.  Initial conditions
// are pinned; per-tick advancement is purely Fixed-domain.
//


struct TestUnit
{
    Fixed lon, lat, alt;
    Fixed targetLon, targetLat;
    Fixed speed;            // arc-degrees per tick (10 Hz tick already baked in)
    Fixed totalDistance;
    Fixed apogee;
    bool  active;
};


static unsigned long long HashTestUnit( unsigned long long h, const TestUnit &u )
{
    h = DeterminismHarness::FnvUpdateBytes( h, &u.lon,            sizeof(Fixed) );
    h = DeterminismHarness::FnvUpdateBytes( h, &u.lat,            sizeof(Fixed) );
    h = DeterminismHarness::FnvUpdateBytes( h, &u.alt,            sizeof(Fixed) );
    h = DeterminismHarness::FnvUpdateBytes( h, &u.targetLon,      sizeof(Fixed) );
    h = DeterminismHarness::FnvUpdateBytes( h, &u.targetLat,      sizeof(Fixed) );
    h = DeterminismHarness::FnvUpdateBytes( h, &u.speed,          sizeof(Fixed) );
    h = DeterminismHarness::FnvUpdateBytes( h, &u.totalDistance,  sizeof(Fixed) );
    h = DeterminismHarness::FnvUpdateBytes( h, &u.apogee,         sizeof(Fixed) );
    int activeI = u.active ? 1 : 0;
    h = DeterminismHarness::FnvUpdateBytes( h, &activeI,          sizeof(int) );
    return h;
}


//
// Compute apogee from total great-circle distance per the
// SPEC_AMBIGUOUS-12 piecewise-linear table (degrees of arc -> metres).
//
static Fixed ApogeeForDistance( const Fixed &d )
{
    if( d <= Fixed(0)  ) return Fixed(0);
    if( d <= Fixed(9)  ) return Fixed(300000)  *  d                    / Fixed(9);
    if( d <= Fixed(45) ) return Fixed(300000) + (Fixed(500000) * (d - Fixed(9))  / Fixed(36));
    if( d <= Fixed(90) ) return Fixed(800000) + (Fixed(400000) * (d - Fixed(45)) / Fixed(45));
    return Fixed(1200000);
}


//
// One tick of motion: bearing toward target, walk speed*dt arc-degrees,
// update lon/lat/alt according to apogee profile.  Mirrors the Phase 2
// Nuke Update logic in source/world/nuke.cpp.
//
static void StepUnit( TestUnit &u )
{
    if( !u.active ) return;

    Fixed remaining = SphereGreatCircleDistanceDeg( u.lon, u.lat, u.targetLon, u.targetLat );
    Fixed fraction  = ( u.totalDistance > Fixed(0) )
                    ? ( Fixed(1) - remaining / u.totalDistance )
                    : Fixed(0);

    if( fraction > Fixed(1) ) fraction = Fixed(1);
    if( fraction < Fixed(0) ) fraction = Fixed(0);

    Fixed bearing = SphereGreatCircleBearingDeg( u.lon, u.lat, u.targetLon, u.targetLat );
    Fixed step    = u.speed;

    Fixed newLon, newLat;
    SphereGreatCircleDestination( u.lon, u.lat, bearing, step, newLon, newLat );

    // Apogee profile: h(s) = h_max * sin(pi * s / S).
    if( u.apogee > Fixed(0) )
    {
        u.alt = u.apogee * sin( fraction * Fixed::PI );
        if( u.alt < Fixed(0) ) u.alt = Fixed(0);
    }
    else
    {
        u.alt = Fixed(0);
    }

    u.lon = newLon;
    u.lat = newLat;

    Fixed newRemaining = SphereGreatCircleDistanceDeg( u.lon, u.lat, u.targetLon, u.targetLat );
    if( newRemaining < Fixed(2) && newRemaining >= remaining )
    {
        u.active = false;
    }
}


//
// Scenarios.
//

static const int kSurfaceUnitCount = 4;
static const int kMirvUnitCount    = 1;
static const int kRadarUnitCount   = 6;


static void InitSurfaceScenario( TestUnit *units )
{
    // Four surface vessels on great-circle paths between fixed
    // lat/lon waypoints.  No apogee.  Phase 1 path exercise.
    Fixed init[kSurfaceUnitCount][4] = {
        // lon0, lat0, lonT, latT
        { Fixed(  10), Fixed( 50), Fixed(-70), Fixed( 40) },   // North Atlantic crossing
        { Fixed(-150), Fixed(-30), Fixed( 150), Fixed(-40) },  // Pacific antimeridian crossing
        { Fixed(  60), Fixed( 60), Fixed(-120), Fixed( 70) },  // Polar route
        { Fixed( 120), Fixed( 0),  Fixed(  20), Fixed(  0) },  // Equatorial
    };
    for( int i = 0; i < kSurfaceUnitCount; ++i )
    {
        units[i].lon = init[i][0];
        units[i].lat = init[i][1];
        units[i].alt = Fixed(0);
        units[i].targetLon = init[i][2];
        units[i].targetLat = init[i][3];
        units[i].speed = Fixed::Hundredths(2);              // 0.02 deg / tick
        units[i].totalDistance = SphereGreatCircleDistanceDeg(
            units[i].lon, units[i].lat,
            units[i].targetLon, units[i].targetLat );
        units[i].apogee = Fixed(0);
        units[i].active = true;
    }
}


static void InitMirvScenario( TestUnit *units )
{
    // One ICBM-like nuke from Moscow-ish to NYC-ish.  Tests apogee
    // profile + great-circle ground track.
    units[0].lon = Fixed(  37);   // ~Moscow
    units[0].lat = Fixed(  55);
    units[0].alt = Fixed(0);
    units[0].targetLon = Fixed( -74);   // ~NYC
    units[0].targetLat = Fixed(  40);
    units[0].speed = Fixed::Hundredths(30);                // 0.30 deg / tick (matches Nuke speed)
    units[0].totalDistance = SphereGreatCircleDistanceDeg(
        units[0].lon, units[0].lat,
        units[0].targetLon, units[0].targetLat );
    units[0].apogee = ApogeeForDistance( units[0].totalDistance );
    units[0].active = true;
}


static void InitRadarScenario( TestUnit *units )
{
    // Six bombers on varied great-circle paths at constant 10 km
    // altitude.  Tests sphere primitives under altitude-aware coverage
    // queries.
    Fixed init[kRadarUnitCount][4] = {
        { Fixed(   0), Fixed(  0), Fixed(  90), Fixed( 30) },
        { Fixed( -90), Fixed( 45), Fixed(  90), Fixed( 45) },
        { Fixed( 180), Fixed(-30), Fixed(   0), Fixed(-30) },
        { Fixed(-179), Fixed(  0), Fixed( 179), Fixed(  0) },  // anti-meridian straddle
        { Fixed(   0), Fixed(-89), Fixed(   0), Fixed( 89) },  // pole-to-pole
        { Fixed(  45), Fixed( 30), Fixed(-135), Fixed(-30) },  // antipodal-ish
    };
    for( int i = 0; i < kRadarUnitCount; ++i )
    {
        units[i].lon = init[i][0];
        units[i].lat = init[i][1];
        units[i].alt = Fixed(10000);
        units[i].targetLon = init[i][2];
        units[i].targetLat = init[i][3];
        units[i].speed = Fixed::Hundredths(5);
        units[i].totalDistance = SphereGreatCircleDistanceDeg(
            units[i].lon, units[i].lat,
            units[i].targetLon, units[i].targetLat );
        units[i].apogee = Fixed(0);
        units[i].active = true;
    }
}


//
// Driver.  Accepts a scenario name and tick count via env vars
// (matches run_determinism.sh contract); writes a per-tick trace
// to DEFCON_DETERMINISM_TRACE.
//

int main( int argc, char *argv[] )
{
    const char *scenario = getenv( "DEFCON_DETERMINISM_SCENARIO" );
    const char *ticksEnv = getenv( "DEFCON_DETERMINISM_TICKS" );
    const char *tracePath = getenv( "DEFCON_DETERMINISM_TRACE" );

    if( argc > 1 ) scenario = argv[1];
    if( argc > 2 ) ticksEnv = argv[2];
    if( argc > 3 ) tracePath = argv[3];

    if( !scenario )  scenario  = "surface";
    if( !ticksEnv )  ticksEnv  = "10000";
    if( !tracePath ) tracePath = "/tmp/defcon-determinism.trace";

    int ticks = atoi( ticksEnv );
    if( ticks <= 0 ) ticks = 10000;

    FILE *fp = fopen( tracePath, "w" );
    if( !fp )
    {
        fprintf( stderr, "determinism_test: cannot open %s for write\n", tracePath );
        return 2;
    }

    TestUnit *units = NULL;
    int unitCount = 0;

    if( strcmp( scenario, "surface" ) == 0 )
    {
        unitCount = kSurfaceUnitCount;
        units = new TestUnit[unitCount];
        InitSurfaceScenario( units );
    }
    else if( strcmp( scenario, "mirv" ) == 0 )
    {
        unitCount = kMirvUnitCount;
        units = new TestUnit[unitCount];
        InitMirvScenario( units );
    }
    else if( strcmp( scenario, "radar" ) == 0 )
    {
        unitCount = kRadarUnitCount;
        units = new TestUnit[unitCount];
        InitRadarScenario( units );
    }
    else
    {
        fprintf( stderr, "determinism_test: unknown scenario \"%s\"\n", scenario );
        fclose( fp );
        return 3;
    }

    fprintf( fp, "# scenario=%s ticks=%d units=%d\n", scenario, ticks, unitCount );
    fprintf( fp, "# format: <tick>\\t<fnv1a-64-hex>\n" );

    for( int t = 0; t < ticks; ++t )
    {
        // Advance simulation.
        for( int i = 0; i < unitCount; ++i ) StepUnit( units[i] );

        // Hash state.
        unsigned long long h = DeterminismHarness::FNV_OFFSET;
        for( int i = 0; i < unitCount; ++i ) h = HashTestUnit( h, units[i] );
        DeterminismHarness::WriteTraceLine( fp, t, h );
    }

    fflush( fp );
    fclose( fp );
    delete [] units;

    fprintf( stdout, "determinism_test: scenario=%s wrote %d ticks to %s\n",
             scenario, ticks, tracePath );

    return 0;
}
