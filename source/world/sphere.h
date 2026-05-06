
#ifndef _included_sphere_h
#define _included_sphere_h

#include "lib/math/fixed.h"
#include "lib/math/vector3.h"

//
// Phase 1 spherical-geometry primitives.
//
// All operations are in the Fixed domain (invariant I-1).  Lon/lat is
// the authoritative position (invariant I-3); ECEF is derived per call.
//
// Earth model (SPEC_AMBIGUOUS-02 resolution): mean sphere with radius
// SPHERE_EARTH_RADIUS_METRES.  Distance helpers return values in
// "degree-of-arc" units to keep the existing AI / range constants
// applicable in Phase 1 (SPEC_AMBIGUOUS-09 resolution); Phase 2 layers
// metres-based helpers on top for altitude/horizon physics.
//
// Pole singularity (SPEC_AMBIGUOUS-10 resolution): targets within
// SPHERE_POLE_EPSILON_DEG of either pole are snapped to the cap edge
// before any bearing computation.
//


//
// Constants.
//

// Mean Earth radius, in metres.  Production constant.
extern const Fixed SPHERE_EARTH_RADIUS_METRES;

// One degree of arc in metres (= R * pi / 180).
extern const Fixed SPHERE_ARC_DEG_TO_M;

// Pole epsilon in degrees.  Targets snap to (90 - eps) latitude.
extern const Fixed SPHERE_POLE_EPSILON_DEG;


//
// Lon/lat <-> ECEF on the unit sphere (radius 1).  Caller scales by
// SPHERE_EARTH_RADIUS_METRES + altitude when a metric ECEF is needed.
//

// Convention: +X out at (lon=0, lat=0); +Y out at the north pole;
// +Z out at (lon=90, lat=0).  Right-handed.
void SphereLonLatToUnit ( const Fixed &lon, const Fixed &lat,
                          Vector3<Fixed> &out );

// Inverse.  In-vector need not be exactly on the unit sphere; the
// routine projects via atan2/asin.
void SphereUnitToLonLat ( const Vector3<Fixed> &v,
                          Fixed &lon, Fixed &lat );


//
// Phase 2: altitude-aware ECEF.  alt is metres above sea level;
// negative for submerged subs.  Returns ECEF in metres.
//
void SphereLonLatAltToECEF ( const Fixed &lon, const Fixed &lat, const Fixed &alt,
                             Vector3<Fixed> &out );

void SphereECEFToLonLatAlt ( const Vector3<Fixed> &v,
                             Fixed &lon, Fixed &lat, Fixed &alt );


//
// Phase 2: radar horizon arc, in *degrees of arc*, for an observer at
// height h_r metres looking at a target at height h_t metres.  Uses
// the standard geometric horizon formula:
//
//   d_r = sqrt( 2 * R * h_r )           (observer to horizon, arc-length)
//   d_t = sqrt( 2 * R * h_t )           (target   to horizon, arc-length)
//   horizon = (d_r + d_t) / R           (radians)  -> degrees of arc
//
// Atmospheric refraction is omitted in Phase 2 (SPEC_AMBIGUOUS-16
// resolution).  Negative heights (submerged sub) return 0.
//
Fixed SphereHorizonArcDeg ( const Fixed &h_observer_m,
                            const Fixed &h_target_m );


//
// Pole-cap snap (SPEC_AMBIGUOUS-10).  Mutates lat in-place.  Lon
// untouched (the cap is a parallel of latitude).
//
void SphereClampOutOfPoleCap ( Fixed &lon, Fixed &lat );


//
// Great-circle distance between two lon/lat points, in *degrees of arc*.
// Implemented via haversine on the Fixed sin/cos primitives.
//
// To convert to metres: multiply by SPHERE_ARC_DEG_TO_M.
//
Fixed SphereGreatCircleDistanceDeg ( const Fixed &lonA, const Fixed &latA,
                                     const Fixed &lonB, const Fixed &latB );

// Squared variant; saves a sqrt in range tests where only ordering
// matters.  Returned in (degrees of arc)^2.
Fixed SphereGreatCircleDistanceDegSqd ( const Fixed &lonA, const Fixed &latA,
                                        const Fixed &lonB, const Fixed &latB );


//
// Initial bearing from A toward B, in degrees.  Result in [-180, 180);
// 0 = north, +90 = east.  Undefined when A is at a pole; caller is
// expected to have applied SphereClampOutOfPoleCap first.
//
Fixed SphereGreatCircleBearingDeg ( const Fixed &lonA, const Fixed &latA,
                                    const Fixed &lonB, const Fixed &latB );


//
// Great-circle destination: starting at (lon, lat), travel arcDeg
// degrees of arc along bearingDeg, return the resulting (lon, lat).
// arcDeg may be negative (reverse).  bearingDeg in degrees.
//
void SphereGreatCircleDestination ( const Fixed &lon, const Fixed &lat,
                                    const Fixed &bearingDeg,
                                    const Fixed &arcDeg,
                                    Fixed &outLon, Fixed &outLat );


//
// Slerp interpolation along the shorter great-circle arc between A and
// B.  t in [0, 1].  Returns (lon, lat) along the arc.
//
void SphereGreatCircleInterpolate ( const Fixed &lonA, const Fixed &latA,
                                    const Fixed &lonB, const Fixed &latB,
                                    const Fixed &t,
                                    Fixed &outLon, Fixed &outLat );


//
// Ray vs unit sphere.  Returns true on hit and writes the nearest
// non-negative t to outT.  Used by the Phase 1 selection geometry that
// gameplay logic shares with GlobeRenderer.
//
bool SphereRaySphereIntersectUnit ( const Vector3<Fixed> &origin,
                                    const Vector3<Fixed> &dir,
                                    Fixed &outT );


#endif
