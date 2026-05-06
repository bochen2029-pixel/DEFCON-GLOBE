
#ifndef _included_globerenderer_h
#define _included_globerenderer_h

#include "lib/math/vector3.h"
#include "lib/math/fixed.h"

class WorldObject;


//
// Phase 0 GlobeRenderer
//
// Read-only 3D-globe view of the unchanged 2D simulation.  Toggled at
// runtime against the existing flat MapRenderer via g_useGlobeRenderer.
// All gameplay mutation paths still flow through MapRenderer; GlobeRenderer
// only knows how to draw the world and convert pixel<->angle for selection.
//
// Pipeline seeded from LobbyRenderer::SetupCamera3d / RenderGlobe.
//


#define     PREFS_GRAPHICS_GLOBERENDERER        "RenderGlobeView"


class GlobeRenderer
{
protected:
    //
    // Orbit camera (spherical) - decoupled from MapRenderer's flat cam.
    //
    float   m_camYaw;           // longitude of camera sub-point, degrees
    float   m_camPitch;         // latitude of camera sub-point, degrees, clamped [-89,89]
    float   m_camDistance;      // distance from earth centre, in unit-radius space
    float   m_camRoll;          // reserved for cinematics, default 0

    // Phase 3: animated camera target.  CenterViewport / CameraCut /
    // wheel zoom write the targets; Update() lerps the current state
    // toward them by 1/m_camSpeed each tick (SPEC_AMBIGUOUS-17 smooth
    // continuous transition + SPEC_AMBIGUOUS-19 auto-follow port).
    float   m_targetYaw;
    float   m_targetPitch;
    float   m_targetDistance;
    int     m_camSpeed;          // higher = slower; 1 = snap; default 200

    Vector3<float> m_camPos;    // last computed camera position (eye)
    Vector3<float> m_camFront;  // last computed forward
    Vector3<float> m_camUp;     // last computed up

    bool    m_dragging;
    float   m_dragMouseX;
    float   m_dragMouseY;

    int     m_pixelX;
    int     m_pixelY;
    int     m_pixelW;
    int     m_pixelH;

    int     m_currentHighlightId;
    int     m_currentSelectionId;

protected:
    void    SetupCamera3d           ();
    void    RenderSphereFill        ();
    void    RenderGuidelines        ();
    void    RenderCoastlines        ();
    void    RenderBorders           ();
    void    RenderObjects           ();
    void    RenderTrails            ();
    void    RenderRadarOverlay      ();
    void    RenderExplosions        ();

    static void LonLatToUnitVector  ( float longitude, float latitude, Vector3<float> &out );

public:
    GlobeRenderer();
    ~GlobeRenderer();

    void    Init                    ();

    void    Update                  ();
    void    Render                  ();

    //
    // Input.  Mirrors MapRenderer entry points so app.cpp can dispatch
    // through a single switch on g_useGlobeRenderer.
    //
    void    UpdateCameraControl     ( float _mouseX, float _mouseY );
    int     GetNearestObjectToMouse ( float _mouseX, float _mouseY );
    void    HandleObjectAction      ( float _mouseX, float _mouseY, int _underMouseId );
    void    HandleSetWaypoint       ( float _mouseX, float _mouseY );
    void    HandleSelectObject      ( int _underMouseId );

    //
    // Pixel <-> angle.  Replaces MapRenderer's inverse-Mercator with
    // ray-vs-sphere intersection (see lib/math/sphere.h in Phase 1; for
    // Phase 0 the math lives inline since sphere.h is not yet created).
    //
    void    ConvertPixelsToAngle    ( float pixelX, float pixelY, float *longitude, float *latitude );
    void    ConvertAngleToPixels    ( float longitude, float latitude, float *pixelX, float *pixelY, bool *behindGlobe = 0 );

    //
    // Misc helpers expected by callers of MapRenderer.
    //
    bool    IsOnScreen              ( float _longitude, float _latitude, float _expandScreen = 2.0f );
    void    CenterViewport          ( float longitude, float latitude, int zoom = 20, int camSpeed = 200 );
    void    CenterViewport          ( int objectId, int zoom = 20, int camSpeed = 200 );
    void    CameraCut               ( float longitude, float latitude, int zoom = 10 );

    int     GetCurrentSelectionId   ();
    void    SetCurrentSelectionId   ( int id );
    int     GetCurrentHighlightId   ();
};


#endif
