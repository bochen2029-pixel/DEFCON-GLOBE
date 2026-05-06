
#ifndef _included_render_util_h
#define _included_render_util_h

//
// Phase 3: small dispatch shim that routes UI-anchor pixel<->angle
// queries to either MapRenderer (flat 2D map) or GlobeRenderer (3D
// globe) depending on g_useGlobeRenderer.
//
// Without this, every call site in source/interface/* that asks
// "what world position is the mouse over" or "where on screen is
// this lon/lat" hardcodes g_app->GetMapRenderer()->...  and silently
// gives wrong answers when the player toggles to globe view.
//
// The two renderer classes already provide the per-renderer math:
//   - MapRenderer::ConvertPixelsToAngle / ConvertAngleToPixels
//   - GlobeRenderer::ConvertPixelsToAngle / ConvertAngleToPixels
// This wrapper just picks the right one and surfaces the
// behindGlobe flag for the globe case (always false in 2D).
//


class RenderUtil
{
public:
    //
    // Mouse pixel -> world (lon, lat).  In globe mode a click that
    // misses the sphere returns lon = lat = 0 (sentinel - callers
    // may want to ignore it or treat as space).
    //
    static void PixelsToAngle ( float pixelX, float pixelY,
                                float *longitude, float *latitude );

    //
    // World (lon, lat) -> screen (pixelX, pixelY).  Sets behindGlobe
    // to true when the point is on the far side of the sphere (in
    // 2D the value is always false).  SPEC_AMBIGUOUS-24 resolution
    // is to hide labels when behindGlobe; callers honour it.
    //
    static void AngleToPixels ( float longitude, float latitude,
                                float *pixelX, float *pixelY,
                                bool *behindGlobe = 0 );

    //
    // Centre the active renderer's viewport on a world position.
    // Wraps MapRenderer::CenterViewport / GlobeRenderer::CenterViewport
    // (SPEC_AMBIGUOUS-19 auto-follow port).
    //
    static void CenterViewport ( float longitude, float latitude,
                                 int zoom = 20, int camSpeed = 200 );
};


#endif
