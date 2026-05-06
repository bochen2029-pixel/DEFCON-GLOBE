#include "lib/universal_include.h"

#include <math.h>

#include "lib/gucci/window_manager.h"
#include "lib/gucci/input.h"
#include "lib/math/vector3.h"
#include "lib/preferences.h"
#include "lib/render/renderer.h"
#include "lib/resource/resource.h"
#include "lib/eclipse/eclipse.h"

#include "app/app.h"
#include "app/globals.h"

#include "world/world.h"
#include "world/earthdata.h"
#include "world/worldobject.h"
#include "world/team.h"
#include "world/date.h"
#include "world/whiteboard.h"
#include "world/sphere.h"

#include "renderer/map_renderer.h"
#include "renderer/globe_renderer.h"


//
// Phase 0 GlobeRenderer
//
// Renders a 3D earth as an alternative view over the unchanged 2D
// simulation.  All world state still uses (lon, lat); altitude is forced
// to zero in Phase 0.  Camera is an orbit camera in spherical coords.
// Picking is ray-vs-sphere in float (Phase 1 promotes to lib/math/sphere.h
// in Fixed for the selection geometry that gameplay uses).
//


GlobeRenderer::GlobeRenderer()
:   m_camYaw(0.0f),
    m_camPitch(20.0f),
    m_camDistance(2.6f),
    m_camRoll(0.0f),
    m_targetYaw(0.0f),
    m_targetPitch(20.0f),
    m_targetDistance(2.6f),
    m_camSpeed(1),
    m_dragging(false),
    m_dragMouseX(0.0f),
    m_dragMouseY(0.0f),
    m_pixelX(0),
    m_pixelY(0),
    m_pixelW(0),
    m_pixelH(0),
    m_currentHighlightId(-1),
    m_currentSelectionId(-1)
{
}


GlobeRenderer::~GlobeRenderer()
{
}


void GlobeRenderer::Init()
{
    m_pixelX = 0;
    m_pixelY = 0;
    m_pixelW = (int) g_windowManager->WindowW();
    m_pixelH = (int) g_windowManager->WindowH();
}


void GlobeRenderer::LonLatToUnitVector( float longitude, float latitude, Vector3<float> &out )
{
    // Match LobbyRenderer::RenderGlobe convention: start at +Z, rotate by lon
    // around Y, then by lat around the local right vector.  Earth radius = 1.
    Vector3<float> p( 0.0f, 0.0f, 1.0f );
    p.RotateAroundY( longitude / 180.0f * M_PI );
    Vector3<float> right = p ^ Vector3<float>::UpVector();
    right.Normalise();
    p.RotateAround( right * (latitude / 180.0f * M_PI) );
    out = p;
}


void GlobeRenderer::SetupCamera3d()
{
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float screenW = g_windowManager->WindowW();
    float screenH = g_windowManager->WindowH();

    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    gluPerspective( fov, screenW / screenH, nearPlane, farPlane );
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();

    // Eye position: orbit at (yaw, pitch, distance) around earth center.
    Vector3<float> eye( 0.0f, 0.0f, m_camDistance );
    eye.RotateAroundX( -m_camPitch / 180.0f * M_PI );
    eye.RotateAroundY( -m_camYaw / 180.0f * M_PI );

    Vector3<float> target( 0.0f, 0.0f, 0.0f );
    Vector3<float> up( 0.0f, 1.0f, 0.0f );

    m_camPos = eye;
    m_camFront = (target - eye);
    m_camFront.Normalise();
    m_camUp = up;

    gluLookAt( eye.x, eye.y, eye.z,
               target.x, target.y, target.z,
               up.x, up.y, up.z );

    glDisable( GL_TEXTURE_2D );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glDisable( GL_LIGHTING );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );
    glClearDepth( 1.0f );
    glClear( GL_DEPTH_BUFFER_BIT );
}


void GlobeRenderer::RenderSphereFill()
{
    // Solid filled sphere in DEFCON water-blue.  Subdivision-3 icosphere
    // would be ideal (SPEC_AMBIGUOUS-21 resolution) but for Phase 0 we use
    // a UV sphere - cheap, the silhouette is hidden behind coastlines.
    const int latSteps = 32;
    const int lonSteps = 64;

    glColor4f( 0.04f, 0.10f, 0.18f, 1.0f );
    glEnable( GL_CULL_FACE );
    glCullFace( GL_BACK );

    for( int i = 0; i < latSteps; ++i )
    {
        float lat0 = -90.0f + 180.0f *  i      / latSteps;
        float lat1 = -90.0f + 180.0f * (i + 1) / latSteps;

        glBegin( GL_TRIANGLE_STRIP );
        for( int j = 0; j <= lonSteps; ++j )
        {
            float lon = -180.0f + 360.0f * j / lonSteps;
            Vector3<float> a, b;
            LonLatToUnitVector( lon, lat1, a );
            LonLatToUnitVector( lon, lat0, b );
            glVertex3fv( a.GetData() );
            glVertex3fv( b.GetData() );
        }
        glEnd();
    }

    glDisable( GL_CULL_FACE );
}


void GlobeRenderer::RenderGuidelines()
{
    glColor4f( 0.15f, 0.40f, 0.15f, 0.20f );

    // Meridians
    for( float lon = -180.0f; lon < 180.0f; lon += 30.0f )
    {
        glBegin( GL_LINE_STRIP );
        for( float lat = -90.0f; lat <= 90.0f; lat += 5.0f )
        {
            Vector3<float> p;
            LonLatToUnitVector( lon, lat, p );
            p *= 1.001f;
            glVertex3fv( p.GetData() );
        }
        glEnd();
    }

    // Parallels
    for( float lat = -60.0f; lat <= 60.0f; lat += 30.0f )
    {
        glBegin( GL_LINE_STRIP );
        for( float lon = -180.0f; lon <= 180.0f; lon += 5.0f )
        {
            Vector3<float> p;
            LonLatToUnitVector( lon, lat, p );
            p *= 1.001f;
            glVertex3fv( p.GetData() );
        }
        glEnd();
    }
}


void GlobeRenderer::RenderCoastlines()
{
    if( !g_app->GetEarthData() ) return;

    glColor4f( 0.0f, 1.0f, 0.0f, 1.0f );

    for( int i = 0; i < g_app->GetEarthData()->m_islands.Size(); ++i )
    {
        Island *island = g_app->GetEarthData()->m_islands[i];
        if( !island ) continue;

        glBegin( GL_LINE_STRIP );
        for( int j = 0; j < island->m_points.Size(); ++j )
        {
            Vector3<float> *thePoint = island->m_points[j];
            Vector3<float> p;
            LonLatToUnitVector( thePoint->x, thePoint->y, p );
            p *= 1.002f;
            glVertex3fv( p.GetData() );
        }
        glEnd();
    }
}


void GlobeRenderer::RenderBorders()
{
    if( !g_app->GetEarthData() ) return;
    if( g_preferences->GetInt( PREFS_GRAPHICS_BORDERS ) != 1 ) return;

    glColor4f( 0.0f, 1.0f, 0.0f, 0.30f );

    for( int i = 0; i < g_app->GetEarthData()->m_borders.Size(); ++i )
    {
        Island *island = g_app->GetEarthData()->m_borders[i];
        if( !island ) continue;

        glBegin( GL_LINE_STRIP );
        for( int j = 0; j < island->m_points.Size(); ++j )
        {
            Vector3<float> *thePoint = island->m_points[j];
            Vector3<float> p;
            LonLatToUnitVector( thePoint->x, thePoint->y, p );
            p *= 1.002f;
            glVertex3fv( p.GetData() );
        }
        glEnd();
    }
}


void GlobeRenderer::RenderObjects()
{
    World *world = g_app->GetWorld();
    if( !world ) return;

    glPointSize( 4.0f );
    glBegin( GL_POINTS );

    for( int i = 0; i < world->m_objects.Size(); ++i )
    {
        if( !world->m_objects.ValidIndex( i ) ) continue;
        WorldObject *obj = world->m_objects[i];
        if( !obj ) continue;

        Team *team = (obj->m_teamId >= 0 && obj->m_teamId < world->m_teams.Size())
                   ? world->m_teams[ obj->m_teamId ] : NULL;

        Colour col = team ? team->GetTeamColour() : Colour(255,255,255);
        glColor4ub( col.m_r, col.m_g, col.m_b, 255 );

        Vector3<float> p;
        LonLatToUnitVector( obj->m_longitude.DoubleValue(),
                            obj->m_latitude.DoubleValue(), p );
        // Hard back-face cull (SPEC_AMBIGUOUS-23 resolution: hard cull in P0).
        Vector3<float> view = m_camPos - p;
        if( (p * view) < 0.0f ) continue;

        p *= 1.005f;
        glVertex3fv( p.GetData() );
    }

    glEnd();
    glPointSize( 1.0f );
}


void GlobeRenderer::RenderTrails()
{
    // Phase 0 stub - trail rendering deferred to Phase 1 once great-circle
    // history exists.
}


void GlobeRenderer::RenderDayNightTerminator()
{
    // Phase 4 day/night terminator.  Darken the half of the sphere
    // facing away from the sun.  Sun direction:
    //   - longitude advances as the world clock does (Earth rotates
    //     360 deg per 86400 game-seconds).
    //   - latitude held at 0 (equinox model; solar declination is a
    //     polish item if needed).
    // Implementation: render the same UV sphere as RenderSphereFill,
    // per-vertex colour = night-tint * smoothstep(-0.2, 0.2, dot(v, sun)).
    World *world = g_app->GetWorld();
    if( !world ) return;

    double seconds = world->m_theDate.m_theDate.DoubleValue();
    double sunLon = (seconds / 86400.0) * 360.0;
    while( sunLon >  180.0 ) sunLon -= 360.0;
    while( sunLon < -180.0 ) sunLon += 360.0;

    Vector3<float> sun;
    LonLatToUnitVector( (float) sunLon, 0.0f, sun );

    const int latSteps = 32;
    const int lonSteps = 64;

    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glDisable( GL_CULL_FACE );

    for( int i = 0; i < latSteps; ++i )
    {
        float lat0 = -90.0f + 180.0f *  i      / latSteps;
        float lat1 = -90.0f + 180.0f * (i + 1) / latSteps;

        glBegin( GL_TRIANGLE_STRIP );
        for( int j = 0; j <= lonSteps; ++j )
        {
            float lon = -180.0f + 360.0f * j / lonSteps;
            Vector3<float> a, b;
            LonLatToUnitVector( lon, lat1, a );
            LonLatToUnitVector( lon, lat0, b );

            // dot in [-1, 1]; remap to [0,1] night intensity via
            // a soft terminator band of ~12 deg.
            float da = a * sun;
            float db = b * sun;
            float ka = 0.5f - 0.5f * da;
            float kb = 0.5f - 0.5f * db;
            // Clamp + smoothstep-ish for a softer terminator edge.
            if( ka < 0 ) ka = 0; if( ka > 1 ) ka = 1;
            if( kb < 0 ) kb = 0; if( kb > 1 ) kb = 1;
            ka = ka * ka * (3.0f - 2.0f * ka);
            kb = kb * kb * (3.0f - 2.0f * kb);
            // Night alpha caps at 0.55 so coastlines remain readable.
            float alphaA = ka * 0.55f;
            float alphaB = kb * 0.55f;

            // Slightly above the surface fill to avoid z-fighting.
            Vector3<float> aa = a * 1.0009f;
            Vector3<float> bb = b * 1.0009f;

            glColor4f( 0.0f, 0.0f, 0.05f, alphaA );
            glVertex3fv( aa.GetData() );
            glColor4f( 0.0f, 0.0f, 0.05f, alphaB );
            glVertex3fv( bb.GetData() );
        }
        glEnd();
    }
}


void GlobeRenderer::RenderRadarOverlay()
{
    // Phase 0 stub.
}


void GlobeRenderer::RenderExplosions()
{
    // Phase 0 stub - billboards aligned to surface normal land in Phase 4.
}


void GlobeRenderer::RenderWhiteBoard()
{
    // Phase 4 whiteboard on the sphere (SPEC_AMBIGUOUS-27 option A).
    // The data model in source/world/whiteboard.h is unchanged - lon/lat
    // points with m_startPoint demarcating strokes.  Each stroke is
    // drawn as a great-circle polyline; segments are subdivided so the
    // arc visibly hugs the surface rather than chord-cutting through.
    World *world = g_app->GetWorld();
    if( !world ) return;

    Team *myTeam = world->GetMyTeam();
    if( !myTeam ) return;

    glLineWidth( 2.0f );
    glDisable( GL_DEPTH_TEST );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    int sizeteams = world->m_teams.Size();
    for( int t = 0; t < sizeteams; ++t )
    {
        Team *team = world->m_teams[ t ];
        bool show = ( team->m_teamId == myTeam->m_teamId )
                 || ( world->IsFriend( myTeam->m_teamId, team->m_teamId ) );
        if( !show ) continue;

        WhiteBoard *wb = &world->m_whiteBoards[ team->m_teamId ];
        const LList<WhiteBoardPoint *> *points = wb->GetListPoints();
        if( !points ) continue;
        int n = points->Size();
        if( n == 0 ) continue;

        Colour colour = team->GetTeamColour();
        glColor4ub( colour.m_r, colour.m_g, colour.m_b, colour.m_a );

        bool inStroke = false;
        WhiteBoardPoint *prev = NULL;

        for( int i = 0; i < n; ++i )
        {
            WhiteBoardPoint *pt = points->GetData( i );

            if( i == 0 || pt->m_startPoint )
            {
                if( inStroke ) { glEnd(); inStroke = false; }
                glBegin( GL_LINE_STRIP );
                inStroke = true;
                Vector3<float> p;
                LonLatToUnitVector( pt->m_longitude, pt->m_latitude, p );
                p *= 1.003f;
                glVertex3fv( p.GetData() );
            }
            else if( prev )
            {
                // Subdivide the great-circle arc from prev to pt.  Use
                // ~one segment per 2 deg of arc; clamps to [1, 32].
                float dLon = pt->m_longitude - prev->m_longitude;
                float dLat = pt->m_latitude  - prev->m_latitude;
                float arcDeg = sqrtf( dLon * dLon + dLat * dLat );
                int subs = (int) (arcDeg / 2.0f);
                if( subs < 1  ) subs = 1;
                if( subs > 32 ) subs = 32;

                for( int k = 1; k <= subs; ++k )
                {
                    float u = (float) k / (float) subs;
                    // True great-circle interpolation via the Phase 1
                    // sphere primitives.  Promotes float->Fixed for the
                    // slerp; result back to float for OpenGL.
                    Fixed outLon, outLat;
                    SphereGreatCircleInterpolate(
                        Fixed::FromDouble( prev->m_longitude ),
                        Fixed::FromDouble( prev->m_latitude  ),
                        Fixed::FromDouble( pt->m_longitude   ),
                        Fixed::FromDouble( pt->m_latitude    ),
                        Fixed::FromDouble( u ),
                        outLon, outLat );
                    Vector3<float> p;
                    LonLatToUnitVector( (float) outLon.DoubleValue(),
                                        (float) outLat.DoubleValue(), p );
                    p *= 1.003f;
                    glVertex3fv( p.GetData() );
                }
            }

            prev = pt;
        }
        if( inStroke ) glEnd();
    }

    glLineWidth( 1.0f );
}


void GlobeRenderer::Update()
{
    UpdateCameraControl( g_inputManager->m_mouseX, g_inputManager->m_mouseY );

    // Phase 3: lerp current camera state toward target at 1/m_camSpeed
    // per tick.  m_camSpeed = 1 collapses to a snap (matches CameraCut).
    if( m_camSpeed > 0 )
    {
        float t = 1.0f / (float) m_camSpeed;
        if( t > 1.0f ) t = 1.0f;

        // Yaw needs short-arc lerp across the +/-180 wrap.
        float dyaw = m_targetYaw - m_camYaw;
        while( dyaw >  180.0f ) dyaw -= 360.0f;
        while( dyaw < -180.0f ) dyaw += 360.0f;
        m_camYaw      += dyaw * t;
        while( m_camYaw >  180.0f ) m_camYaw -= 360.0f;
        while( m_camYaw < -180.0f ) m_camYaw += 360.0f;

        m_camPitch    += (m_targetPitch    - m_camPitch)    * t;
        m_camDistance += (m_targetDistance - m_camDistance) * t;
    }
}


void GlobeRenderer::Render()
{
    SetupCamera3d();

    glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    RenderSphereFill();
    RenderDayNightTerminator();   // Phase 4: shade night side
    RenderGuidelines();
    RenderCoastlines();
    RenderBorders();
    RenderObjects();
    RenderTrails();
    RenderRadarOverlay();
    RenderExplosions();
    RenderWhiteBoard();           // Phase 4: geodesic stroke arcs

    // Restore 2D for any UI overlay paths that follow.  UI overlay
    // reprojection is Phase 3 - Phase 0 acceptably mis-aligns it (per
    // docs/DESIGN_v1.md section 26 out-of-scope).
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glOrtho( 0, g_windowManager->WindowW(), g_windowManager->WindowH(), 0, -1, 1 );
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    glDisable( GL_DEPTH_TEST );
}


void GlobeRenderer::UpdateCameraControl( float _mouseX, float _mouseY )
{
    // Trackball drag (SPEC_AMBIGUOUS-18 resolution: trackball).
    bool rmb = g_inputManager->m_rmb;
    if( rmb && !m_dragging )
    {
        m_dragging = true;
        m_dragMouseX = _mouseX;
        m_dragMouseY = _mouseY;
    }
    else if( !rmb && m_dragging )
    {
        m_dragging = false;
    }
    else if( m_dragging )
    {
        // Drag: snap target to follow the mouse (camSpeed = 1 during
        // active drag so the camera tracks 1:1 without lag).
        float dx = _mouseX - m_dragMouseX;
        float dy = _mouseY - m_dragMouseY;
        m_targetYaw   -= dx * 0.30f;
        m_targetPitch += dy * 0.30f;
        if( m_targetPitch >  89.0f ) m_targetPitch =  89.0f;
        if( m_targetPitch < -89.0f ) m_targetPitch = -89.0f;
        while( m_targetYaw >  180.0f ) m_targetYaw -= 360.0f;
        while( m_targetYaw < -180.0f ) m_targetYaw += 360.0f;
        m_camSpeed = 1;     // snap during drag
        m_dragMouseX = _mouseX;
        m_dragMouseY = _mouseY;
    }

    // Mouse wheel zoom: smooth interpolation per SPEC_AMBIGUOUS-17
    // (smooth continuous transition).  Wheel writes m_targetDistance;
    // Update() lerps m_camDistance toward it over a few ticks.
    int wheel = g_inputManager->m_mouseZ;
    if( wheel != 0 )
    {
        m_targetDistance *= (wheel > 0) ? 0.9f : 1.1f;
        if( m_targetDistance < 1.05f ) m_targetDistance = 1.05f;
        if( m_targetDistance > 8.0f  ) m_targetDistance = 8.0f;
        m_camSpeed = 6;     // smooth zoom over ~6 ticks
    }
}


//
// Ray-vs-sphere pixel <-> angle.
//
static bool RaySphereHit( const Vector3<float> &origin, const Vector3<float> &dir,
                          float radius, Vector3<float> &hit )
{
    // |origin + t*dir|^2 = r^2  =>  t^2 + 2 (origin.dir) t + (|origin|^2 - r^2) = 0
    float b = origin * dir;
    float c = (origin * origin) - radius * radius;
    float disc = b * b - c;
    if( disc < 0.0f ) return false;
    float t = -b - sqrtf( disc );
    if( t < 0.0f ) t = -b + sqrtf( disc );
    if( t < 0.0f ) return false;
    hit = origin + dir * t;
    return true;
}


void GlobeRenderer::ConvertPixelsToAngle( float pixelX, float pixelY,
                                          float *longitude, float *latitude )
{
    // Build a ray from camera through the pixel.  We re-derive the camera
    // basis in float to avoid plumbing GL matrix readback.
    float screenW = g_windowManager->WindowW();
    float screenH = g_windowManager->WindowH();
    float fov = 45.0f;
    float aspect = screenW / screenH;
    float tanHalf = tanf( fov * 0.5f * M_PI / 180.0f );

    // NDC-like coords in [-1,1], with y flipped (screen y grows downward).
    float ndcX =  (2.0f * pixelX / screenW - 1.0f) * tanHalf * aspect;
    float ndcY = -(2.0f * pixelY / screenH - 1.0f) * tanHalf;

    Vector3<float> right = m_camFront ^ m_camUp;
    right.Normalise();
    Vector3<float> up = right ^ m_camFront;
    up.Normalise();

    Vector3<float> dir = m_camFront + right * ndcX + up * ndcY;
    dir.Normalise();

    Vector3<float> hit;
    if( !RaySphereHit( m_camPos, dir, 1.0f, hit ) )
    {
        // Miss - return a sentinel that callers treat as out-of-world.
        if( longitude ) *longitude = 0.0f;
        if( latitude  ) *latitude  = 0.0f;
        return;
    }

    // Hit -> (lon, lat).  Inverse of LonLatToUnitVector: latitude is asin(y),
    // longitude is atan2(x, z) in our +Z-forward convention.
    float lat = asinf( hit.y ) * 180.0f / M_PI;
    float lon = atan2f( hit.x, hit.z ) * 180.0f / M_PI;
    if( longitude ) *longitude = lon;
    if( latitude  ) *latitude  = lat;
}


void GlobeRenderer::ConvertAngleToPixels( float longitude, float latitude,
                                          float *pixelX, float *pixelY,
                                          bool *behindGlobe )
{
    Vector3<float> p;
    LonLatToUnitVector( longitude, latitude, p );

    Vector3<float> view = m_camPos - p;
    bool behind = (p * view) < 0.0f;
    if( behindGlobe ) *behindGlobe = behind;

    // Project via gluProject would be cleaner but our GL state is volatile -
    // do it manually using the basis we cached in SetupCamera3d.
    Vector3<float> right = m_camFront ^ m_camUp;
    right.Normalise();
    Vector3<float> up = right ^ m_camFront;
    up.Normalise();

    Vector3<float> rel = p - m_camPos;
    float z = rel * m_camFront;
    if( z <= 0.001f ) z = 0.001f;
    float x = (rel * right) / z;
    float y = (rel * up)    / z;

    float screenW = g_windowManager->WindowW();
    float screenH = g_windowManager->WindowH();
    float fov = 45.0f;
    float aspect = screenW / screenH;
    float tanHalf = tanf( fov * 0.5f * M_PI / 180.0f );

    if( pixelX ) *pixelX = (x / (tanHalf * aspect) + 1.0f) * 0.5f * screenW;
    if( pixelY ) *pixelY = (1.0f - (y / tanHalf + 1.0f) * 0.5f) * screenH;
}


int GlobeRenderer::GetNearestObjectToMouse( float _mouseX, float _mouseY )
{
    World *world = g_app->GetWorld();
    if( !world ) return -1;

    // Build the same ray as ConvertPixelsToAngle.
    float screenW = g_windowManager->WindowW();
    float screenH = g_windowManager->WindowH();
    float fov = 45.0f;
    float aspect = screenW / screenH;
    float tanHalf = tanf( fov * 0.5f * M_PI / 180.0f );
    float ndcX =  (2.0f * _mouseX / screenW - 1.0f) * tanHalf * aspect;
    float ndcY = -(2.0f * _mouseY / screenH - 1.0f) * tanHalf;
    Vector3<float> right = m_camFront ^ m_camUp; right.Normalise();
    Vector3<float> up = right ^ m_camFront; up.Normalise();
    Vector3<float> dir = m_camFront + right * ndcX + up * ndcY;
    dir.Normalise();

    int   nearestId = -1;
    float nearestT  = 1e9f;
    float pickRadius = 0.015f * m_camDistance;     // screen-scaled

    for( int i = 0; i < world->m_objects.Size(); ++i )
    {
        if( !world->m_objects.ValidIndex( i ) ) continue;
        WorldObject *obj = world->m_objects[i];
        if( !obj || !obj->m_selectable ) continue;

        Vector3<float> centre;
        LonLatToUnitVector( obj->m_longitude.DoubleValue(),
                            obj->m_latitude.DoubleValue(), centre );

        // Hard cull back of globe (SPEC_AMBIGUOUS-20 resolution).
        Vector3<float> view = m_camPos - centre;
        if( (centre * view) < 0.0f ) continue;

        // Ray-sphere against the per-object selection sphere.
        Vector3<float> oc = m_camPos - centre;
        float b = oc * dir;
        float c = (oc * oc) - pickRadius * pickRadius;
        float disc = b * b - c;
        if( disc < 0.0f ) continue;
        float t = -b - sqrtf( disc );
        if( t < 0.0f ) continue;
        if( t < nearestT )
        {
            nearestT = t;
            nearestId = obj->m_objectId;
        }
    }

    return nearestId;
}


void GlobeRenderer::HandleSelectObject( int _underMouseId )
{
    m_currentSelectionId = _underMouseId;
}


void GlobeRenderer::HandleObjectAction( float _mouseX, float _mouseY, int _underMouseId )
{
    // Phase 0: routes the action through MapRenderer's existing handler so
    // unit commands behave identically.  Phase 3 will own this directly.
    g_app->GetMapRenderer()->HandleObjectAction( _mouseX, _mouseY, _underMouseId );
}


void GlobeRenderer::HandleSetWaypoint( float _mouseX, float _mouseY )
{
    g_app->GetMapRenderer()->HandleSetWaypoint( _mouseX, _mouseY );
}


bool GlobeRenderer::IsOnScreen( float _longitude, float _latitude, float _expandScreen )
{
    bool behind = false;
    float px, py;
    ConvertAngleToPixels( _longitude, _latitude, &px, &py, &behind );
    if( behind ) return false;
    float screenW = g_windowManager->WindowW();
    float screenH = g_windowManager->WindowH();
    return px >= -screenW * (_expandScreen - 1.0f)
        && px <  screenW * _expandScreen
        && py >= -screenH * (_expandScreen - 1.0f)
        && py <  screenH * _expandScreen;
}


void GlobeRenderer::CenterViewport( float longitude, float latitude, int zoom, int camSpeed )
{
    // Phase 3 auto-follow: animate over camSpeed ticks per
    // SPEC_AMBIGUOUS-19 resolution.
    m_targetYaw   = longitude;
    m_targetPitch = latitude;
    if( m_targetPitch >  89.0f ) m_targetPitch =  89.0f;
    if( m_targetPitch < -89.0f ) m_targetPitch = -89.0f;

    if( zoom > 0 )
    {
        // Map MapRenderer's "zoom = 1..30" to a globe distance.  20 is
        // theatre, 1 is tactical-tight.  Linear is good enough for now.
        float d = 1.05f + (float) zoom * 0.07f;
        if( d < 1.05f ) d = 1.05f;
        if( d > 8.0f  ) d = 8.0f;
        m_targetDistance = d;
    }

    if( camSpeed <= 0 ) camSpeed = 1;
    m_camSpeed = camSpeed;
}


void GlobeRenderer::CenterViewport( int objectId, int zoom, int camSpeed )
{
    World *world = g_app->GetWorld();
    if( !world ) return;
    WorldObject *obj = world->GetWorldObject( objectId );
    if( !obj ) return;
    CenterViewport( obj->m_longitude.DoubleValue(),
                    obj->m_latitude.DoubleValue(), zoom, camSpeed );
}


void GlobeRenderer::CameraCut( float longitude, float latitude, int zoom )
{
    // Cut = snap.  CenterViewport with camSpeed=1 collapses the lerp.
    CenterViewport( longitude, latitude, zoom, 1 );
    m_camYaw      = m_targetYaw;
    m_camPitch    = m_targetPitch;
    m_camDistance = m_targetDistance;
}


int  GlobeRenderer::GetCurrentSelectionId() { return m_currentSelectionId; }
void GlobeRenderer::SetCurrentSelectionId( int id ) { m_currentSelectionId = id; }
int  GlobeRenderer::GetCurrentHighlightId() { return m_currentHighlightId; }
