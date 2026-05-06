#include "lib/universal_include.h"

#include "app/app.h"
#include "app/globals.h"

#include "renderer/map_renderer.h"
#include "renderer/globe_renderer.h"
#include "renderer/render_util.h"


void RenderUtil::PixelsToAngle( float pixelX, float pixelY,
                                float *longitude, float *latitude )
{
    if( g_useGlobeRenderer )
    {
        g_app->GetGlobeRenderer()->ConvertPixelsToAngle( pixelX, pixelY, longitude, latitude );
    }
    else
    {
        g_app->GetMapRenderer()->ConvertPixelsToAngle( pixelX, pixelY, longitude, latitude );
    }
}


void RenderUtil::AngleToPixels( float longitude, float latitude,
                                float *pixelX, float *pixelY,
                                bool *behindGlobe )
{
    if( g_useGlobeRenderer )
    {
        g_app->GetGlobeRenderer()->ConvertAngleToPixels( longitude, latitude, pixelX, pixelY, behindGlobe );
    }
    else
    {
        if( behindGlobe ) *behindGlobe = false;
        g_app->GetMapRenderer()->ConvertAngleToPixels( longitude, latitude, pixelX, pixelY );
    }
}


void RenderUtil::CenterViewport( float longitude, float latitude, int zoom, int camSpeed )
{
    if( g_useGlobeRenderer )
    {
        g_app->GetGlobeRenderer()->CenterViewport( longitude, latitude, zoom, camSpeed );
    }
    else
    {
        g_app->GetMapRenderer()->CenterViewport( longitude, latitude, zoom, camSpeed );
    }
}
