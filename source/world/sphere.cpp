#include "lib/universal_include.h"

#include "lib/math/fixed.h"
#include "lib/math/vector3.h"

#include "world/sphere.h"


//
// Constants
//

// Mean Earth radius (SPEC_AMBIGUOUS-02 resolution).
const Fixed SPHERE_EARTH_RADIUS_METRES( 6371000 );

// One degree of arc in metres.  R * pi / 180 = 6371000 * 3.14159265 / 180.
// Computed at the literal level to avoid runtime init order issues.
const Fixed SPHERE_ARC_DEG_TO_M = Fixed::FromDouble( 111194.926644558738 );

// Pole cap epsilon: 0.01 degrees - the smallest non-zero latitude
// resolvable by the existing 360x200 RadarGrid (SPEC_AMBIGUOUS-10).
const Fixed SPHERE_POLE_EPSILON_DEG = Fixed::Hundredths( 1 );


//
// Local helpers - all in the Fixed domain.
//

namespace
{
    const Fixed kZero      ( 0 );
    const Fixed kOne       ( 1 );
    const Fixed kHalf      = Fixed::Hundredths( 50 );
    const Fixed kTwo       ( 2 );
    const Fixed k90        ( 90 );
    const Fixed k180       ( 180 );
    const Fixed k360       ( 360 );

    // pi (twice piOverTwo from the Fixed namespace - approx 3.14159).
    const Fixed kPi        = Fixed::FromDouble( 3.14159265358979 );
    const Fixed kPiOver180 = Fixed::FromDouble( 0.01745329251994 );
    const Fixed k180OverPi = Fixed::FromDouble( 57.29577951308232 );

    inline Fixed DegToRad( const Fixed &d ) { return d * kPiOver180; }
    inline Fixed RadToDeg( const Fixed &r ) { return r * k180OverPi; }

    // Wrap longitude to [-180, 180).
    Fixed WrapLon( Fixed l )
    {
        while( l <  -k180 ) l += k360;
        while( l >=  k180 ) l -= k360;
        return l;
    }

    // Clamp latitude to [-90, 90].
    Fixed ClampLat( Fixed l )
    {
        if( l >  k90 ) return  k90;
        if( l < -k90 ) return -k90;
        return l;
    }
}


//
// Lon/lat <-> ECEF on the unit sphere
//

void SphereLonLatToUnit( const Fixed &lon, const Fixed &lat, Vector3<Fixed> &out )
{
    Fixed lonR = DegToRad( lon );
    Fixed latR = DegToRad( lat );
    Fixed cl   = cos( latR );
    out.x = cl * cos( lonR );
    out.y = sin( latR );
    out.z = cl * sin( lonR );
}


void SphereUnitToLonLat( const Vector3<Fixed> &v, Fixed &lon, Fixed &lat )
{
    // lat = asin(y).  Caller is responsible for v being roughly on the
    // unit sphere.  For v.y outside [-1,1] (e.g. accumulated drift),
    // Fixed asin saturates safely at +-pi/2.
    lat = RadToDeg( asin( v.y ) );
    lon = RadToDeg( atan2( v.z, v.x ) );
}


void SphereClampOutOfPoleCap( Fixed & /*lon*/, Fixed &lat )
{
    Fixed cap = k90 - SPHERE_POLE_EPSILON_DEG;
    if( lat >  cap ) lat =  cap;
    if( lat < -cap ) lat = -cap;
}


//
// Haversine great-circle distance.  Returns degrees of arc.
//

Fixed SphereGreatCircleDistanceDeg( const Fixed &lonA, const Fixed &latA,
                                    const Fixed &lonB, const Fixed &latB )
{
    Fixed dLat = DegToRad( latB - latA );
    Fixed dLon = DegToRad( lonB - lonA );
    Fixed sLat = sin( dLat * kHalf );
    Fixed sLon = sin( dLon * kHalf );
    Fixed a = sLat * sLat
            + cos( DegToRad( latA ) ) * cos( DegToRad( latB ) ) * sLon * sLon;
    if( a < kZero ) a = kZero;
    if( a > kOne  ) a = kOne;
    Fixed c = kTwo * asin( sqrt( a ) );
    return RadToDeg( c );
}


Fixed SphereGreatCircleDistanceDegSqd( const Fixed &lonA, const Fixed &latA,
                                       const Fixed &lonB, const Fixed &latB )
{
    Fixed d = SphereGreatCircleDistanceDeg( lonA, latA, lonB, latB );
    return d * d;
}


//
// Bearing.
//

Fixed SphereGreatCircleBearingDeg( const Fixed &lonA, const Fixed &latA,
                                   const Fixed &lonB, const Fixed &latB )
{
    Fixed phi1 = DegToRad( latA );
    Fixed phi2 = DegToRad( latB );
    Fixed dLon = DegToRad( lonB - lonA );

    Fixed y = sin( dLon ) * cos( phi2 );
    Fixed x = cos( phi1 ) * sin( phi2 )
            - sin( phi1 ) * cos( phi2 ) * cos( dLon );

    return WrapLon( RadToDeg( atan2( y, x ) ) );
}


//
// Forward azimuth: walk arcDeg degrees of arc from (lon, lat) on bearing.
//

void SphereGreatCircleDestination( const Fixed &lon, const Fixed &lat,
                                   const Fixed &bearingDeg,
                                   const Fixed &arcDeg,
                                   Fixed &outLon, Fixed &outLat )
{
    Fixed phi1 = DegToRad( lat );
    Fixed lam1 = DegToRad( lon );
    Fixed brg  = DegToRad( bearingDeg );
    Fixed d    = DegToRad( arcDeg );

    Fixed sd = sin( d );
    Fixed cd = cos( d );
    Fixed sp = sin( phi1 );
    Fixed cp = cos( phi1 );

    Fixed sinPhi2 = sp * cd + cp * sd * cos( brg );
    if( sinPhi2 >  kOne  ) sinPhi2 =  kOne;
    if( sinPhi2 < -kOne  ) sinPhi2 = -kOne;
    Fixed phi2 = asin( sinPhi2 );

    Fixed lam2 = lam1 + atan2( sin( brg ) * sd * cp,
                               cd - sp * sinPhi2 );

    outLat = ClampLat( RadToDeg( phi2 ) );
    outLon = WrapLon ( RadToDeg( lam2 ) );
}


//
// Slerp - interpolate along the shortest great-circle arc.
//

void SphereGreatCircleInterpolate( const Fixed &lonA, const Fixed &latA,
                                   const Fixed &lonB, const Fixed &latB,
                                   const Fixed &t,
                                   Fixed &outLon, Fixed &outLat )
{
    // Convert to ECEF, slerp, convert back.
    Vector3<Fixed> a, b;
    SphereLonLatToUnit( lonA, latA, a );
    SphereLonLatToUnit( lonB, latB, b );

    // Angle between them.
    Fixed dot = a.x * b.x + a.y * b.y + a.z * b.z;
    if( dot >  kOne  ) dot =  kOne;
    if( dot < -kOne  ) dot = -kOne;
    Fixed omega = acos( dot );

    if( omega == kZero )
    {
        outLon = lonA;
        outLat = latA;
        return;
    }

    Fixed sinO = sin( omega );
    Fixed wA = sin( ( kOne - t ) * omega ) / sinO;
    Fixed wB = sin( t * omega ) / sinO;

    Vector3<Fixed> p( a.x * wA + b.x * wB,
                      a.y * wA + b.y * wB,
                      a.z * wA + b.z * wB );

    SphereUnitToLonLat( p, outLon, outLat );
}


//
// Ray vs unit sphere.
//

//
// Phase 2: altitude-aware ECEF.  Surface position scaled by
// (R + alt); negative alt is sub-surface (submerged sub).
//

void SphereLonLatAltToECEF( const Fixed &lon, const Fixed &lat, const Fixed &alt,
                            Vector3<Fixed> &out )
{
    Vector3<Fixed> unit;
    SphereLonLatToUnit( lon, lat, unit );
    Fixed r = SPHERE_EARTH_RADIUS_METRES + alt;
    out.x = unit.x * r;
    out.y = unit.y * r;
    out.z = unit.z * r;
}


void SphereECEFToLonLatAlt( const Vector3<Fixed> &v,
                            Fixed &lon, Fixed &lat, Fixed &alt )
{
    Fixed mag = sqrt( v.x * v.x + v.y * v.y + v.z * v.z );
    if( mag == kZero )
    {
        lon = kZero; lat = kZero; alt = -SPHERE_EARTH_RADIUS_METRES;
        return;
    }
    Vector3<Fixed> unit( v.x / mag, v.y / mag, v.z / mag );
    SphereUnitToLonLat( unit, lon, lat );
    alt = mag - SPHERE_EARTH_RADIUS_METRES;
}


//
// Radar horizon arc.  Returns degrees of arc per the SPEC_AMBIGUOUS-09
// unit choice; callers in source/world/radargrid.cpp can use it
// directly against degree-of-arc range constants.
//

Fixed SphereHorizonArcDeg( const Fixed &h_observer_m,
                           const Fixed &h_target_m )
{
    Fixed hR = h_observer_m;
    Fixed hT = h_target_m;
    if( hR < kZero ) hR = kZero;
    if( hT < kZero ) hT = kZero;

    // Observer-to-horizon arc length, metres.
    Fixed dR = sqrt( kTwo * SPHERE_EARTH_RADIUS_METRES * hR );
    Fixed dT = sqrt( kTwo * SPHERE_EARTH_RADIUS_METRES * hT );
    Fixed arcM = dR + dT;

    // Convert arc-length (m) to degrees: arcM / R = radians;
    // radians * 180/pi = degrees.
    return ( arcM / SPHERE_EARTH_RADIUS_METRES ) * k180OverPi;
}


bool SphereRaySphereIntersectUnit( const Vector3<Fixed> &origin,
                                   const Vector3<Fixed> &dir,
                                   Fixed &outT )
{
    Fixed b = origin.x * dir.x + origin.y * dir.y + origin.z * dir.z;
    Fixed c = (origin.x * origin.x + origin.y * origin.y + origin.z * origin.z) - kOne;
    Fixed disc = b * b - c;
    if( disc < kZero ) return false;
    Fixed s = sqrt( disc );
    Fixed t0 = -b - s;
    Fixed t1 = -b + s;
    if( t0 >= kZero )      { outT = t0; return true; }
    if( t1 >= kZero )      { outT = t1; return true; }
    return false;
}
