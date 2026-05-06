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


void GlobeRenderer::RenderRadarOverlay()
{
    // Phase 0 stub.
}


void GlobeRenderer::RenderExplosions()
{
    // Phase 0 stub - billboards aligned to surface normal land in Phase 4.
}


void GlobeRenderer::Update()
{
    UpdateCameraControl( g_inputManager->m_mouseX, g_inputManager->m_mouseY );
}


void GlobeRenderer::Render()
{
    SetupCamera3d();

    glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    RenderSphereFill();
    RenderGuidelines();
    RenderCoastlines();
    RenderBorders();
    RenderObjects();
    RenderTrails();
    RenderRadarOverlay();
    RenderExplosions();

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
        float dx = _mouseX - m_dragMouseX;
        float dy = _mouseY - m_dragMouseY;
        m_camYaw   -= dx * 0.30f;
        m_camPitch += dy * 0.30f;
        if( m_camPitch >  89.0f ) m_camPitch =  89.0f;
        if( m_camPitch < -89.0f ) m_camPitch = -89.0f;
        while( m_camYaw >  180.0f ) m_camYaw -= 360.0f;
        while( m_camYaw < -180.0f ) m_camYaw += 360.0f;
        m_dragMouseX = _mouseX;
        m_dragMouseY = _mouseY;
    }

    // Mouse wheel zoom.
    int wheel = g_inputManager->m_mouseZ;
    if( wheel != 0 )
    {
        m_camDistance *= (wheel > 0) ? 0.9f : 1.1f;
        if( m_camDistance < 1.05f ) m_camDistance = 1.05f;
        if( m_camDistance > 8.0f  ) m_camDistance = 8.0f;
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


void GlobeRenderer::CenterViewport( float longitude, float latitude, int /*zoom*/, int /*camSpeed*/ )
{
    m_camYaw   = longitude;
    m_camPitch = latitude;
    if( m_camPitch >  89.0f ) m_camPitch =  89.0f;
    if( m_camPitch < -89.0f ) m_camPitch = -89.0f;
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


void GlobeRenderer::CameraCut( float longitude, float latitude, int /*zoom*/ )
{
    m_camYaw   = longitude;
    m_camPitch = latitude;
}


int  GlobeRenderer::GetCurrentSelectionId() { return m_currentSelectionId; }
void GlobeRenderer::SetCurrentSelectionId( int id ) { m_currentSelectionId = id; }
int  GlobeRenderer::GetCurrentHighlightId() { return m_currentHighlightId; }
